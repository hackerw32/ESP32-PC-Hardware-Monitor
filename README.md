# ESP32-S3 PC Hardware Monitor

A physical dashboard for real-time monitoring of PC hardware statistics (CPU, RAM, and GPU usage). The project uses an ESP32-S3 microcontroller to receive data via USB from a Python script running on the host computer.

The interface allows navigation through multiple pages and live history graphs using a 4x4 matrix keypad.

## Features
- Page A: Overview (Live percentage of CPU, RAM, and GPU usage).
- Page B: Live CPU History Graph (30-second window).
- Page C: Live RAM History Graph (30-second window).
- Page D: Live GPU History Graph (30-second window).
- Background Logging: Data history is updated even when the user is viewing a different page.

## Hardware Requirements
- ESP32-S3 Development Board (e.g., N16R8)
- 1.3" OLED Display (SH1106 I2C Driver)
- 4x4 Matrix Keypad
- Jumper Wires (Female-to-Female)
- USB Type-C Cable

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

## Software Requirements

### 1. PC Side (Python)
Install Python 3 and the required libraries via terminal:
Command: pip install pyserial psutil gputil

### 2. Microcontroller Side (Arduino IDE)
Install the ESP32 board package and the following libraries:
- Adafruit GFX Library
- Adafruit SH110X
- Keypad (by Mark Stanley)

## Installation and Usage

1. Upload Firmware: Open the .ino file in Arduino IDE, select ESP32S3 Dev Module and the correct COM port (UART), then Upload.
2. Serial Connection: Ensure the Arduino Serial Monitor is closed before running the Python script.
3. Configure Python: Set the COM_PORT variable in pc_monitor.py to match your device (e.g., COM3).
4. Run: Execute the Python script. Data will begin streaming to the device immediately.

## Controls
Use the 4x4 Keypad to switch views:
- [A] -> Summary View
- [B] -> CPU Graph
- [C] -> RAM Graph
- [D] -> GPU Graph
