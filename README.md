# ESP32-S3 PC Hardware Monitor (Pro Edition)

A professional-grade physical dashboard for real-time monitoring of PC hardware statistics. This project features an automated installer, auto-port detection, and background execution with Windows startup integration.

The system displays live CPU, RAM, and GPU usage with high-fidelity history graphs on a 1.3" OLED display, controlled by a 4x4 matrix keypad.

## Key Features
- Automated Setup: One-click installer for dependencies and startup configuration.
- Auto-Detect: Python script automatically finds the correct ESP32 COM port.
- Stealth Mode: Runs as a windowless background process (.pyw).
- Live Graphs: 30-second rolling history for CPU, RAM, and GPU.
- Persistent Logging: Data is collected even when switching between different display pages.

## Hardware Requirements
- ESP32-S3 Development Board (e.g., N16R8)
- 1.3" OLED Display (SH1106 I2C Driver)
- 4x4 Matrix Keypad
- USB Type-C Data Cable

## Wiring Diagram

### OLED Display (I2C)
| OLED Pin | ESP32-S3 Pin | Description |
| :--- | :--- | :--- |
| VCC | 3V3 | Power Supply (3.3V) |
| GND | GND | Ground |
| SDA | GPIO 8 | Data Line |
| SCL | GPIO 9 | Clock Line |

### 4x4 Keypad
| Keypad Pin | ESP32-S3 Pin |
| :--- | :--- |
| R1 (Row 1) | GPIO 10 |
| R2 (Row 2) | GPIO 11 |
| R3 (Row 3) | GPIO 12 |
| R4 (Row 4) | GPIO 13 |
| C1 (Col 1) | GPIO 15 |
| C2 (Col 2) | GPIO 16 |
| C3 (Col 3) | GPIO 17 |
| C4 (Col 4) | GPIO 18 |

## Installation

### 1. Firmware (ESP32)
1. Open the Arduino sketch.
2. Install the following libraries via Library Manager: Adafruit GFX, Adafruit SH110X, and Keypad (by Mark Stanley).
3. Select "ESP32S3 Dev Module" and your COM port.
4. Upload the code to your ESP32-S3.

### 2. PC Software (Automated Setup)
1. Ensure Python 3 is installed and added to your system PATH.
2. Run the `setup.py` script provided in this repository.
3. The installer will:
   - Install required Python libraries (pyserial, psutil, gputil).
   - Create an application folder in your User directory.
   - Generate the background monitor script (pc_monitor.pyw).
   - Create a Windows Startup shortcut for automatic execution.

## Controls
Navigate through the dashboard using the 4x4 Keypad:
- [A] Summary View: Displays current CPU, RAM, and GPU usage percentages.
- [B] CPU Graph: 30-second live history graph of CPU load.
- [C] RAM Graph: 30-second live history graph of RAM usage.
- [D] GPU Graph: 30-second live history graph of GPU load.

## Project Structure
- /Arduino_Code: Contains the C++ firmware for the ESP32-S3.
- setup.py: The GUI-based installer for Windows.
- pc_monitor.py: The source code for the PC-to-ESP32 data bridge.
- README.md: Documentation and setup guide.
