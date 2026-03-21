import socket
import psutil
import GPUtil
import time
import subprocess
import platform
import wmi
import threading
import tkinter as tk
from tkinter import ttk
import pystray
from PIL import Image, ImageDraw
import ctypes, sys
import pythoncom

# Κρύβει το console window αμέσως (Windows only)
if sys.platform == "win32":
    ctypes.windll.user32.ShowWindow(ctypes.windll.kernel32.GetConsoleWindow(), 0)

# --- ΒΑΛΕ ΕΔΩ ΤΗΝ IP ΠΟΥ ΣΟΥ ΕΔΕΙΞΕ Η ΟΘΟΝΗ ΤΟΥ ESP32 ---
ESP32_IP = "192.168.1.92"
UDP_PORT = 4242

# =====================================================================
# Globals για stats (ενημερώνονται από το monitor thread)
# =====================================================================
stats = {
    "cpu_pct": 0, "cpu_temp": 0, "cpu_freq": 0,
    "gpu_pct": 0, "gpu_temp": 0,
    "ram_pct": 0, "status": "Starting...",
    "packets_sent": 0,
}
running = True


# =====================================================================
# Hardware functions
# =====================================================================
def get_cpu_name():
    try:
        out = subprocess.check_output("wmic cpu get name", shell=True).decode().split('\n')[1].strip()
        name = out.replace("(R)", "").replace("(TM)", "").replace(" CPU", "").replace(" Processor", "")
        return name.split('@')[0].strip().replace("Intel Core", "Intel")[:21]
    except Exception:
        return platform.processor()[:21]


def get_ram_speed():
    try:
        out = subprocess.check_output("wmic memorychip get speed", shell=True).decode().strip().split('\n')
        for line in out:
            if line.strip().isdigit():
                return int(line.strip())
    except Exception:
        pass
    return 0


def get_cpu_temp():
    """Δοκιμάζει LHM → OHM → MSAcpi για θερμοκρασία CPU."""
    for namespace in [r"root\LibreHardwareMonitor", r"root\OpenHardwareMonitor"]:
        try:
            w = wmi.WMI(namespace=namespace)
            for s in w.Sensor():
                if s.SensorType == 'Temperature' and 'package' in s.Name.lower():
                    if s.Value and s.Value > 0:
                        return int(s.Value)
            for s in w.Sensor():
                if s.SensorType == 'Temperature' and 'cpu' in s.Identifier.lower():
                    if s.Value and s.Value > 0:
                        return int(s.Value)
        except Exception:
            pass
    try:
        w = wmi.WMI(namespace=r"root\WMI")
        for zone in w.MSAcpi_ThermalZoneTemperature():
            t = int((zone.CurrentTemperature / 10.0) - 273.15)
            if 20 < t < 110:
                return t
    except Exception:
        pass
    return 0


# =====================================================================
# Monitor loop (τρέχει σε background thread)
# =====================================================================
def monitor_loop():
    global running
    # Αρχικοποίηση COM για αυτό το thread (απαιτείται από WMI/pywin32)
    pythoncom.CoInitialize()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cpu_name   = get_cpu_name()
    ram_speed  = get_ram_speed()
    counter    = 0

    stats["status"] = f"Sending to {ESP32_IP}..."

    while running:
        try:
            if counter % 5 == 0:
                ram_total = int(psutil.virtual_memory().total / (1024**3))
                gpus = GPUtil.getGPUs()
                gpu_name     = gpus[0].name.replace("NVIDIA","Nvidia").replace("GeForce ","").strip()[:21] if gpus else "Unknown GPU"
                vram_total   = int(gpus[0].memoryTotal / 1024) if gpus else 0
                pkt = f"S:{cpu_name}|{gpu_name}|{ram_speed}|{ram_total}|{vram_total}\n"
                sock.sendto(pkt.encode(), (ESP32_IP, UDP_PORT))
                time.sleep(0.05)

            cpu_temp = get_cpu_temp()
            cpu_pct  = int(psutil.cpu_percent(interval=None))
            cpu_freq = int(psutil.cpu_freq().current)
            ram_pct  = int(psutil.virtual_memory().percent)
            ram_used = int((psutil.virtual_memory().used / (1024**3)) * 10)

            gpus = GPUtil.getGPUs()
            gpu_pct  = int(gpus[0].load * 100)        if gpus else 0
            gpu_temp = int(gpus[0].temperature)        if gpus else 0
            vram_used = int((gpus[0].memoryUsed/1024)*10) if gpus else 0

            pkt = f"D:{cpu_pct},{cpu_temp},{gpu_pct},{gpu_temp},{ram_pct},{cpu_freq},{vram_used},{ram_used}\n"
            sock.sendto(pkt.encode(), (ESP32_IP, UDP_PORT))
            time.sleep(0.05)

            cores = psutil.cpu_percent(interval=None, percpu=True)
            pkt = "C:" + ",".join(str(int(c)) for c in cores) + "\n"
            sock.sendto(pkt.encode(), (ESP32_IP, UDP_PORT))

            # Ενημέρωση stats για UI
            stats.update({
                "cpu_pct": cpu_pct, "cpu_temp": cpu_temp, "cpu_freq": cpu_freq,
                "gpu_pct": gpu_pct, "gpu_temp": gpu_temp,
                "ram_pct": ram_pct,
                "status": f"Sending to {ESP32_IP}",
                "packets_sent": stats["packets_sent"] + 1,
            })

            counter += 1
            time.sleep(1)

        except Exception as e:
            stats["status"] = f"Error: {e}"
            time.sleep(1)


# =====================================================================
# Tray Icon
# =====================================================================
def create_tray_icon():
    """Δημιουργεί ένα απλό 64x64 icon για το system tray."""
    img = Image.new("RGB", (64, 64), color=(20, 20, 30))
    d = ImageDraw.Draw(img)
    # Outer circle
    d.ellipse([4, 4, 60, 60], outline=(0, 200, 100), width=3)
    # CPU bar (left half)
    d.rectangle([12, 20, 28, 44], outline=(0, 200, 100), width=1)
    d.rectangle([12, 32, 28, 44], fill=(0, 200, 100))
    # GPU bar (right half)
    d.rectangle([36, 20, 52, 44], outline=(0, 150, 255), width=1)
    d.rectangle([36, 36, 52, 44], fill=(0, 150, 255))
    return img


# =====================================================================
# GUI Window
# =====================================================================
class MonitorApp:
    def __init__(self, root):
        self.root = root
        self.root.title("PC WiFi Monitor")
        self.root.geometry("340x230")
        self.root.resizable(False, False)
        self.root.configure(bg="#0f0f1a")
        self.root.protocol("WM_DELETE_WINDOW", self.minimize_to_tray)

        # Override minimize button to go to tray
        self.root.bind("<Unmap>", self._on_unmap)
        self._minimized = False

        self._build_ui()
        self._build_tray()
        self._update_ui()

    def _build_ui(self):
        bg = "#0f0f1a"
        fg = "#00c864"
        fg2 = "#0096ff"
        head = "#1a1a2e"

        # Title bar
        title_frame = tk.Frame(self.root, bg=head, height=36)
        title_frame.pack(fill=tk.X)
        tk.Label(title_frame, text="  PC WiFi Monitor", bg=head, fg=fg,
                 font=("Consolas", 12, "bold")).pack(side=tk.LEFT, pady=6)
        tk.Label(title_frame, text=f"→ {ESP32_IP}", bg=head, fg="#555577",
                 font=("Consolas", 9)).pack(side=tk.RIGHT, padx=10, pady=8)

        # Stats grid
        grid = tk.Frame(self.root, bg=bg, pady=8)
        grid.pack(fill=tk.BOTH, expand=True, padx=16)

        def make_row(parent, label, row, color):
            tk.Label(parent, text=label, bg=bg, fg="#888899",
                     font=("Consolas", 9)).grid(row=row, column=0, sticky="w", pady=3)
            var = tk.StringVar(value="---")
            tk.Label(parent, textvariable=var, bg=bg, fg=color,
                     font=("Consolas", 11, "bold"), width=18, anchor="w").grid(row=row, column=1, sticky="w")
            return var

        self.cpu_var  = make_row(grid, "CPU Usage :", 0, fg)
        self.cput_var = make_row(grid, "CPU Temp  :", 1, fg)
        self.gpu_var  = make_row(grid, "GPU Usage :", 2, fg2)
        self.gput_var = make_row(grid, "GPU Temp  :", 3, fg2)
        self.ram_var  = make_row(grid, "RAM Usage :", 4, "#ff9900")
        self.pkts_var = make_row(grid, "Packets   :", 5, "#555577")

        # Status bar
        self.status_var = tk.StringVar(value="Starting...")
        status_bar = tk.Frame(self.root, bg=head, height=26)
        status_bar.pack(fill=tk.X, side=tk.BOTTOM)
        tk.Label(status_bar, textvariable=self.status_var, bg=head, fg="#555577",
                 font=("Consolas", 8)).pack(side=tk.LEFT, padx=8, pady=4)

        btn_frame = tk.Frame(status_bar, bg=head)
        btn_frame.pack(side=tk.RIGHT, padx=6, pady=2)
        tk.Button(btn_frame, text="Hide to Tray", command=self.minimize_to_tray,
                  bg="#1a1a2e", fg=fg, font=("Consolas", 8), relief=tk.FLAT,
                  activebackground="#2a2a4e", activeforeground=fg, bd=0,
                  padx=6, pady=2, cursor="hand2").pack()

    def _build_tray(self):
        icon_img = create_tray_icon()
        menu = pystray.Menu(
            pystray.MenuItem("Show Monitor", self._show_window, default=True),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Exit", self._exit_app),
        )
        self.tray = pystray.Icon("PC Monitor", icon_img, "PC WiFi Monitor", menu)
        # Run tray in background thread
        self.tray_thread = threading.Thread(target=self.tray.run, daemon=True)
        self.tray_thread.start()

    def _on_unmap(self, event):
        """Πιάνει το minimize button του OS."""
        if event.widget == self.root and not self._minimized:
            self._minimized = True
            self.root.after(50, self.minimize_to_tray)

    def minimize_to_tray(self):
        self._minimized = True
        self.root.withdraw()  # Κρύβει το παράθυρο

    def _show_window(self, icon=None, item=None):
        self._minimized = False
        self.root.after(0, self._do_show)

    def _do_show(self):
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()

    def _exit_app(self, icon=None, item=None):
        global running
        running = False
        self.tray.stop()
        self.root.after(0, self.root.destroy)

    def _update_ui(self):
        """Ανανεώνει τα labels κάθε 1 δευτερόλεπτο."""
        self.cpu_var.set(f"{stats['cpu_pct']:3d}%  {stats['cpu_freq']} MHz")
        self.cput_var.set(f"{stats['cpu_temp']:3d}°C")
        self.gpu_var.set(f"{stats['gpu_pct']:3d}%")
        self.gput_var.set(f"{stats['gpu_temp']:3d}°C")
        self.ram_var.set(f"{stats['ram_pct']:3d}%")
        self.pkts_var.set(f"{stats['packets_sent']}")
        self.status_var.set(stats["status"])
        self.root.after(1000, self._update_ui)


# =====================================================================
# Main
# =====================================================================
if __name__ == "__main__":
    # Εκκίνηση monitor loop σε background thread
    monitor_thread = threading.Thread(target=monitor_loop, daemon=True)
    monitor_thread.start()

    # Εκκίνηση GUI
    root = tk.Tk()
    app = MonitorApp(root)
    root.mainloop()
