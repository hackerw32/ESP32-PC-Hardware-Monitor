import os
import sys
import subprocess
import tkinter as tk
from tkinter import messagebox

# Ο κώδικας που θα τρέχει στο παρασκήνιο (Payload)
MONITOR_CODE = """import serial
import serial.tools.list_ports
import psutil
import GPUtil
import time

BAUD_RATE = 115200

def auto_detect_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = port.description.upper()
        if "CH343" in desc or "UART" in desc or "SERIAL" in desc or "CP210" in desc:
            return port.device
    return ports[0].device if ports else None

COM_PORT = auto_detect_port()
if not COM_PORT: exit()

try:
    ser = serial.Serial(COM_PORT, BAUD_RATE)
except Exception:
    exit()

time.sleep(2)

while True:
    try:
        cpu = int(psutil.cpu_percent(interval=None))
        ram = int(psutil.virtual_memory().percent)
        gpus = GPUtil.getGPUs()
        gpu = int(gpus[0].load * 100) if gpus else 0

        data_string = f"C:{cpu},R:{ram},G:{gpu}\\n"
        ser.write(data_string.encode('utf-8'))
        time.sleep(1)
    except Exception:
        time.sleep(1)
"""

class SetupApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 PC Monitor - Setup")
        self.root.geometry("400x300")
        self.root.resizable(False, False)

        # UI Elements
        self.lbl_title = tk.Label(root, text="ESP32 PC Monitor Installer", font=("Arial", 14, "bold"))
        self.lbl_title.pack(pady=15)

        self.txt_log = tk.Text(root, height=8, width=45, state=tk.DISABLED, bg="#f0f0f0")
        self.txt_log.pack(pady=5)

        self.btn_install = tk.Button(root, text="Εγκατάσταση", font=("Arial", 12), bg="#4CAF50", fg="white", command=self.run_setup)
        self.btn_install.pack(pady=15)

    def log(self, message):
        self.txt_log.config(state=tk.NORMAL)
        self.txt_log.insert(tk.END, message + "\n")
        self.txt_log.see(tk.END)
        self.txt_log.config(state=tk.DISABLED)
        self.root.update()

    def run_setup(self):
        self.btn_install.config(state=tk.DISABLED)
        
        try:
            # 1. Εγκατάσταση Βιβλιοθηκών
            self.log("[1/4] Έλεγχος/Εγκατάσταση βιβλιοθηκών...")
            reqs = ["pyserial", "psutil", "gputil"]
            for req in reqs:
                self.log(f" -> Εγκατάσταση {req}...")
                subprocess.check_call([sys.executable, "-m", "pip", "install", req, "--quiet"])

            # 2. Δημιουργία Φακέλου στον Χρήστη
            self.log("[2/4] Δημιουργία φακέλου εφαρμογής...")
            user_dir = os.path.expanduser("~")
            app_dir = os.path.join(user_dir, "ESP32_PC_Monitor")
            if not os.path.exists(app_dir):
                os.makedirs(app_dir)

            # 3. Δημιουργία του αρχείου .pyw
            self.log("[3/4] Αντιγραφή αρχείων συστήματος...")
            script_path = os.path.join(app_dir, "pc_monitor.pyw")
            with open(script_path, "w", encoding="utf-8") as f:
                f.write(MONITOR_CODE)

            # 4. Δημιουργία Συντόμευσης στο Startup
            self.log("[4/4] Προσθήκη στην εκκίνηση των Windows...")
            startup_dir = os.path.join(os.getenv("APPDATA"), "Microsoft", "Windows", "Start Menu", "Programs", "Startup")
            shortcut_path = os.path.join(startup_dir, "ESP32_Monitor.lnk")
            
            # Χρήση VBScript για δημιουργία συντόμευσης χωρίς extra libraries
            vbs_path = os.path.join(app_dir, "create_shortcut.vbs")
            vbs_code = f"""
Set oWS = WScript.CreateObject("WScript.Shell")
sLinkFile = "{shortcut_path}"
Set oLink = oWS.CreateShortcut(sLinkFile)
oLink.TargetPath = "pythonw.exe"
oLink.Arguments = "{script_path}"
oLink.WorkingDirectory = "{app_dir}"
oLink.WindowStyle = 7
oLink.Save
"""
            with open(vbs_path, "w") as f:
                f.write(vbs_code)
            
            subprocess.call(["cscript.exe", vbs_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.remove(vbs_path) # Διαγραφή του VBScript αφού κάνει τη δουλειά του

            self.log("\n✅ Η εγκατάσταση ολοκληρώθηκε!")
            messagebox.showinfo("Επιτυχία", "Η εγκατάσταση ολοκληρώθηκε με επιτυχία!\nΤο πρόγραμμα θα τρέχει αυτόματα σε κάθε εκκίνηση του υπολογιστή.")
            self.root.destroy()

        except Exception as e:
            self.log(f"❌ Σφάλμα: {e}")
            messagebox.showerror("Σφάλμα", f"Κάτι πήγε στραβά:\n{e}")
            self.btn_install.config(state=tk.NORMAL)

if __name__ == "__main__":
    root = tk.Tk()
    app = SetupApp(root)
    root.mainloop()
