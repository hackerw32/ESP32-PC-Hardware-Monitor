import socket
import psutil
import GPUtil
import time
import subprocess
import platform
import wmi # ΝΕΟ: Για την αληθινή θερμοκρασία!

# --- ΒΑΛΕ ΕΔΩ ΤΗΝ IP ΠΟΥ ΣΟΥ ΕΔΕΙΞΕ Η ΟΘΟΝΗ ΤΟΥ ESP32 ---
ESP32_IP = "192.168.1.92" 
UDP_PORT = 4242



def get_cpu_name():
    try:
        output = subprocess.check_output("wmic cpu get name", shell=True).decode().split('\n')[1].strip()
        name = output.replace("(R)", "").replace("(TM)", "").replace(" CPU", "").replace(" Processor", "")
        name = name.split('@')[0].strip().replace("Intel Core", "Intel") 
        return name[:21] 
    except Exception:
        return platform.processor()[:21]

def get_ram_speed():
    try:
        output = subprocess.check_output("wmic memorychip get speed", shell=True).decode().strip().split('\n')
        for line in output:
            if line.strip().isdigit(): return int(line.strip())
    except Exception:
        pass
    return 0 

# Σύνδεση με το WMI για την αληθινή θερμοκρασία
try:
    w = wmi.WMI(namespace="root\\LibreHardwareMonitor")
    print("✅ Συνδέθηκε με το LibreHardwareMonitor!")
except Exception:
    print("⚠️ Δεν βρέθηκε το LibreHardwareMonitor! Η θερμοκρασία CPU θα δείχνει 0.")
    w = None

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
MY_CPU_NAME = get_cpu_name()
MY_RAM_SPEED = get_ram_speed()

print(f"🚀 ΑΣΥΡΜΑΤΗ αποστολή στην IP: {ESP32_IP}...")
counter = 0

while True:
    try:
        if counter % 5 == 0:
            ram_total_gb = int(psutil.virtual_memory().total / (1024**3))
            gpus = GPUtil.getGPUs()
            if gpus:
                gpu_name = gpus[0].name.replace("NVIDIA", "Nvidia").replace("GeForce ", "").replace("  ", " ").strip()[:21]
                vram_total_gb = int(gpus[0].memoryTotal / 1024)
            else:
                gpu_name = "Unknown GPU"
                vram_total_gb = 0
            static_packet = f"S:{MY_CPU_NAME}|{gpu_name}|{MY_RAM_SPEED}|{ram_total_gb}|{vram_total_gb}\n"
            sock.sendto(static_packet.encode('utf-8'), (ESP32_IP, UDP_PORT))
            time.sleep(0.05) 

        # --- ΔΙΑΒΑΣΜΑ ΑΛΗΘΙΝΗΣ ΘΕΡΜΟΚΡΑΣΙΑΣ CPU ---
        cpu_temp = 0
        if w:
            try:
                for sensor in w.Sensor():
                    if sensor.SensorType == 'Temperature' and 'cpu' in sensor.Identifier.lower() and 'package' in sensor.Name.lower():
                        cpu_temp = int(sensor.Value)
                        break
                if cpu_temp == 0: # Αν δεν βρει το 'package', παίρνει το πρώτο core
                    for sensor in w.Sensor():
                        if sensor.SensorType == 'Temperature' and 'cpu' in sensor.Identifier.lower():
                            cpu_temp = int(sensor.Value)
                            break
            except:
                pass

        cpu_pct = int(psutil.cpu_percent(interval=None))
        cpu_freq = int(psutil.cpu_freq().current)
        ram_pct = int(psutil.virtual_memory().percent)
        ram_used = int((psutil.virtual_memory().used / (1024**3)) * 10) 
        
        gpus = GPUtil.getGPUs()
        if gpus:
            gpu_pct = int(gpus[0].load * 100)
            gpu_temp = int(gpus[0].temperature)
            vram_used = int((gpus[0].memoryUsed / 1024) * 10)
        else:
            gpu_pct, gpu_temp, vram_used = 0, 0, 0

        dynamic_packet = f"D:{cpu_pct},{cpu_temp},{gpu_pct},{gpu_temp},{ram_pct},{cpu_freq},{vram_used},{ram_used}\n"
        sock.sendto(dynamic_packet.encode('utf-8'), (ESP32_IP, UDP_PORT))
        time.sleep(0.05)
        
        cores = psutil.cpu_percent(interval=None, percpu=True)
        cores_str = ",".join([str(int(c)) for c in cores])
        cores_packet = f"C:{cores_str}\n"
        sock.sendto(cores_packet.encode('utf-8'), (ESP32_IP, UDP_PORT))
        
        counter += 1
        time.sleep(1) 
        
    except Exception as e:
        time.sleep(1)