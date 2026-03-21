#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// =========================================================================
const char* HARDCODED_SSID = "Dickinsons"; 
const char* HARDCODED_PASS = "paraskevi1994";
// Βάλε εδώ τη MAC Address της μητρικής σου για το Wake-on-LAN! (π.χ. "AA:BB:CC:DD:EE:FF")
const char* PC_MAC_ADDRESS = "6C:29:95:2C:1A:19"; 
// =========================================================================

Adafruit_SH1106G displayLarge = Adafruit_SH1106G(128, 64, &Wire, -1);
TwoWire I2C_Small(1); 
Adafruit_SSD1306 displaySmall(128, 64, &I2C_Small, -1);

#define BUZZER_PIN 21

const byte ROWS = 4; const byte COLS = 4; 
char keys[ROWS][COLS] = {
  {'1','2','3','A'}, {'4','5','6','B'}, {'7','8','9','C'}, {'*','0','#','D'}
};
byte rowPins[ROWS] = {10, 11, 12, 13}; byte colPins[COLS] = {15, 16, 17, 18};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

enum SystemMode { MAIN_MENU, PC_MONITOR, WEATHER, HACKING, GAME, ANIMATION, SETTINGS_MENU, WIFI_MENU, NETWORK_INFO, T9_INPUT };
SystemMode currentApp = PC_MONITOR;
char pcView = '1';

const int TOTAL_MENU_ITEMS = 6;
String menuItems[TOTAL_MENU_ITEMS] = {"PC Monitor", "Weather Clock", "Hacking Mode", "Mini Game", "Animations", "Settings"};
int menuSelection = 0; int menuTopIndex = 0;
int settingsSelection = 0; int wifiSelection = 0;

String mySSID = ""; String myPASS = "";
WiFiUDP udp;
unsigned int localUdpPort = 4242; 
char incomingPacket[255]; 

String cpuName = "Waiting Wi-Fi..."; String gpuName = "Waiting Wi-Fi...";
int valRamSpeed = 0; int valRamTotal = 0; int valVramTotal = 0;
int valCpuPct = 0, valCpuTemp = 0, valGpuPct = 0, valGpuTemp = 0, valRamPct = 0, valCpuFreq = 0; 
float valVramUsed = 0.0, valRamUsed = 0.0;
int core_usage[28] = {0}; 
const int HISTORY_SIZE = 30; int cpu_hist[HISTORY_SIZE] = {0}; int ram_hist[HISTORY_SIZE] = {0}; int gpu_hist[HISTORY_SIZE] = {0};

int sessionMinCpuTemp = 999; int sessionMaxCpuTemp = 0;
int maxCpuTemp = 89; int maxGpuTemp = 85;

unsigned long lastAlarmTime = 0; unsigned long lastDrawTime = 0; unsigned long lastPacketTime = 0; 
int tempSettingStep = 0; bool isTypingTemp = false; String tempStrCpu = ""; String tempStrGpu = "";
bool needsUpdate = false;

// Προστασία Οθόνης (Screensaver)
bool isSleeping = false;
int saverX = 10; int saverY = 10; int saverDX = 2; int saverDY = 2;

const char* t9_map[10] = { " 0", ".,?!1", "abc2", "def3", "ghi4", "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9" };
String* t9_targetString; String t9_buffer = "";
char t9_pendingChar = 0; char t9_lastKey = 0; int t9_tapIndex = 0;
unsigned long t9_lastTapTime = 0; int t9_capsMode = 0; 

void playTone(int freq, int duration_ms);
void playBootSound(); void playSuccessSound(); void playAlarmSound();
void updateDisplay(); void drawSmallScreen(); void drawLargeScreen();
void writeEEPROMString(int startAddr, String data); String readEEPROMString(int startAddr);
void connectWiFi(); void processT9Timer(); void drawScreensaver();
void wakeMyPC(); 

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  Wire.begin(8, 9); displayLarge.begin(0x3C, true);
  I2C_Small.begin(1, 2); displaySmall.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  EEPROM.begin(256); 
  maxCpuTemp = EEPROM.readInt(0); maxGpuTemp = EEPROM.readInt(4); 
  if (maxCpuTemp < 20 || maxCpuTemp > 120) maxCpuTemp = 89;
  if (maxGpuTemp < 20 || maxGpuTemp > 120) maxGpuTemp = 85;
  
  if (String(HARDCODED_SSID).length() > 0) {
    mySSID = String(HARDCODED_SSID); myPASS = String(HARDCODED_PASS);
  } else {
    mySSID = readEEPROMString(10); myPASS = readEEPROMString(50);
  }

  playBootSound(); // ΝΕΟΣ ΗΧΟΣ SCI-FI!
  connectWiFi(); 
  updateDisplay();
}

void loop() {
  needsUpdate = false; 

  if (currentApp == T9_INPUT) { processT9Timer(); }

  char key = keypad.getKey();
  if (key) {
    playTone(3000, 30); // Απλό κλικ
    isSleeping = false; // Αν πατήσεις κουμπί, ξυπνάει η οθόνη!
    lastPacketTime = millis(); // Κάνουμε reset το χρονόμετρο ύπνου
    
    if (key == '*') { currentApp = MAIN_MENU; tempSettingStep = 0; menuSelection = 0; menuTopIndex = 0; t9_pendingChar = 0; needsUpdate = true; } 
    else if (currentApp == MAIN_MENU) {
      if (key == 'A') { if (menuSelection > 0) menuSelection--; if (menuSelection < menuTopIndex) menuTopIndex = menuSelection; needsUpdate = true; } 
      else if (key == 'B') { if (menuSelection < TOTAL_MENU_ITEMS - 1) menuSelection++; if (menuSelection >= menuTopIndex + 4) menuTopIndex++; needsUpdate = true; }
      else if (key == 'D') { 
        if (menuSelection == 0) currentApp = PC_MONITOR; else if (menuSelection == 1) currentApp = WEATHER;
        else if (menuSelection == 2) currentApp = HACKING; else if (menuSelection == 3) currentApp = GAME; else if (menuSelection == 4) currentApp = ANIMATION;
        else if (menuSelection == 5) { currentApp = SETTINGS_MENU; settingsSelection = 0; }
        needsUpdate = true;
      }
    }
    else if (currentApp == SETTINGS_MENU) {
      if (key == 'A') { if (settingsSelection > 0) settingsSelection--; needsUpdate = true; }
      else if (key == 'B') { if (settingsSelection < 2) settingsSelection++; needsUpdate = true; }
      else if (key == 'D') {
        if (settingsSelection == 0) { currentApp = WIFI_MENU; wifiSelection = 0; }
        else if (settingsSelection == 1) currentApp = NETWORK_INFO;
        else if (settingsSelection == 2) { wakeMyPC(); } // ΝΕΟ: Εντολή Wake-on-LAN!
        needsUpdate = true;
      }
    }
    else if (currentApp == WIFI_MENU) {
      if (key == 'A') { if (wifiSelection > 0) wifiSelection--; needsUpdate = true; }
      else if (key == 'B') { if (wifiSelection < 1) wifiSelection++; needsUpdate = true; }
      else if (key == 'D') {
        currentApp = T9_INPUT; t9_buffer = (wifiSelection == 0) ? mySSID : myPASS; 
        t9_targetString = (wifiSelection == 0) ? &mySSID : &myPASS;
        t9_pendingChar = 0; t9_lastKey = 0; t9_capsMode = 0; needsUpdate = true;
      }
    }
    else if (currentApp == T9_INPUT) {
      if (key == 'D') {
        if (t9_pendingChar != 0) { t9_buffer += t9_pendingChar; t9_pendingChar = 0; }
        *t9_targetString = t9_buffer; writeEEPROMString((wifiSelection == 0) ? 10 : 50, *t9_targetString); 
        playSuccessSound(); // ΝΕΟΣ ΗΧΟΣ ΕΠΙΤΥΧΙΑΣ!
        displayLarge.clearDisplay(); displayLarge.setCursor(10,25); displayLarge.print("Saved! Rebooting..."); displayLarge.display();
        delay(1500); ESP.restart(); 
      }
      else if (key == 'A') {
        if (t9_pendingChar != 0) { t9_pendingChar = 0; } 
        else if (t9_buffer.length() > 0) { t9_buffer.remove(t9_buffer.length() - 1); } 
        t9_lastKey = 0; needsUpdate = true;
      }
      else if (key == '#') { t9_capsMode = (t9_capsMode + 1) % 3; needsUpdate = true; }
      else if (key >= '0' && key <= '9') {
        int numKey = key - '0';
        if (key == t9_lastKey && (millis() - t9_lastTapTime < 1000)) { t9_tapIndex++; } 
        else {
          if (t9_pendingChar != 0) { t9_buffer += t9_pendingChar; } 
          if (t9_capsMode == 1 && t9_buffer.length() > 0) t9_capsMode = 0; 
          t9_tapIndex = 0; t9_lastKey = key;
        }
        t9_lastTapTime = millis();
        int mapLen = strlen(t9_map[numKey]); t9_pendingChar = t9_map[numKey][t9_tapIndex % mapLen];
        if (t9_capsMode > 0 && t9_pendingChar >= 'a' && t9_pendingChar <= 'z') { t9_pendingChar -= 32; }
        needsUpdate = true;
      }
    }
    else if (currentApp == PC_MONITOR) {
      if (tempSettingStep == 0) {
        if (key == '0') { sessionMinCpuTemp = 999; sessionMaxCpuTemp = 0; playTone(3000, 50); needsUpdate = true; }
        else if (key == '#') {
          tempSettingStep = 1; isTypingTemp = false;
          tempStrCpu = String(maxCpuTemp); tempStrGpu = String(maxGpuTemp); needsUpdate = true;
        } 
        else if (key == '1' || key == 'A' || key == 'B' || key == 'C' || key == 'D') { pcView = key; needsUpdate = true; }
      } 
      else { 
        if (key == '#') {
          if (tempStrCpu.length() > 0) maxCpuTemp = tempStrCpu.toInt();
          if (tempStrGpu.length() > 0) maxGpuTemp = tempStrGpu.toInt();
          EEPROM.writeInt(0, maxCpuTemp); EEPROM.writeInt(4, maxGpuTemp); EEPROM.commit(); 
          tempSettingStep = 0; playSuccessSound(); needsUpdate = true;
        }
        else if (key == 'A') { if (tempSettingStep == 2) { tempSettingStep = 1; isTypingTemp = false; needsUpdate = true; } }
        else if (key == 'B') { if (tempSettingStep == 1) { tempSettingStep = 2; isTypingTemp = false; needsUpdate = true; } }
        else if (key >= '0' && key <= '9') {
          if (!isTypingTemp) { if (tempSettingStep == 1) tempStrCpu = ""; else tempStrGpu = ""; isTypingTemp = true; }
          if (tempSettingStep == 1 && tempStrCpu.length() < 3) tempStrCpu += key;
          else if (tempSettingStep == 2 && tempStrGpu.length() < 3) tempStrGpu += key;
          needsUpdate = true;
        }
      }
    }
  }

  // --- UDP RECEIVE ---
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(incomingPacket, 255);
    if (len > 0) incomingPacket[len] = 0; 
    String data = String(incomingPacket); data.trim();

    if (data.startsWith("S:")) {
      String payload = data.substring(2);
      int p1 = payload.indexOf('|'); int p2 = payload.indexOf('|', p1 + 1);
      int p3 = payload.indexOf('|', p2 + 1); int p4 = payload.indexOf('|', p3 + 1);
      if (p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0) {
        cpuName = payload.substring(0, p1); gpuName = payload.substring(p1 + 1, p2);
        valRamSpeed = payload.substring(p2 + 1, p3).toInt(); valRamTotal = payload.substring(p3 + 1, p4).toInt(); valVramTotal = payload.substring(p4 + 1).toInt();
        lastPacketTime = millis(); isSleeping = false;
      }
    }
    else if (data.startsWith("D:")) {
      int vU = 0, rU = 0;
      if (sscanf(data.c_str(), "D:%d,%d,%d,%d,%d,%d,%d,%d", &valCpuPct, &valCpuTemp, &valGpuPct, &valGpuTemp, &valRamPct, &valCpuFreq, &vU, &rU) == 8) {
        valVramUsed = vU / 10.0; valRamUsed = rU / 10.0;
        if (valCpuTemp > 0) {
          if (valCpuTemp < sessionMinCpuTemp) sessionMinCpuTemp = valCpuTemp;
          if (valCpuTemp > sessionMaxCpuTemp) sessionMaxCpuTemp = valCpuTemp;
        }
        for(int i = 0; i < HISTORY_SIZE - 1; i++){ cpu_hist[i] = cpu_hist[i+1]; gpu_hist[i] = gpu_hist[i+1]; ram_hist[i] = ram_hist[i+1]; }
        cpu_hist[HISTORY_SIZE - 1] = valCpuPct; gpu_hist[HISTORY_SIZE - 1] = valGpuPct; ram_hist[HISTORY_SIZE - 1] = valRamPct;
        lastPacketTime = millis(); isSleeping = false; needsUpdate = true; 
      }
    }
    else if (data.startsWith("C:")) {
      String payload = data.substring(2); int startIdx = 0;
      for (int i = 0; i < 28; i++) {
        int commaIdx = payload.indexOf(',', startIdx);
        if (commaIdx == -1) { core_usage[i] = payload.substring(startIdx).toInt(); break; } 
        else { core_usage[i] = payload.substring(startIdx, commaIdx).toInt(); startIdx = commaIdx + 1; }
      }
      needsUpdate = true;
    }
  }

  // --- ΕΛΕΓΧΟΣ SCREENSAVER (Αν περάσει 1 λεπτό χωρίς δεδομένα) ---
  if (millis() - lastPacketTime > 60000 && WiFi.status() == WL_CONNECTED) {
    isSleeping = true;
    cpuName = "Waiting Wi-Fi..."; gpuName = "Waiting Wi-Fi...";
    valCpuPct = 0; valCpuTemp = 0; valGpuPct = 0; valGpuTemp = 0; valRamPct = 0; valCpuFreq = 0; valVramUsed = 0.0; valRamUsed = 0.0; valRamSpeed = 0; valRamTotal = 0; valVramTotal = 0;
    sessionMinCpuTemp = 999; sessionMaxCpuTemp = 0;
    for(int i = 0; i < HISTORY_SIZE; i++){ cpu_hist[i] = 0; gpu_hist[i] = 0; ram_hist[i] = 0; }
    for(int i = 0; i < 28; i++){ core_usage[i] = 0; } 
  }

  // Σχεδιασμός Screensaver ή Κανονικής Οθόνης
  if (millis() - lastDrawTime > (isSleeping ? 100 : 500)) needsUpdate = true; // Το Screensaver ανανεώνεται πιο γρήγορα
  
  if (needsUpdate) { 
    if (isSleeping) {
      drawScreensaver();
    } else {
      updateDisplay(); 
    }
    lastDrawTime = millis(); 
  }

  if (millis() - lastAlarmTime > 3000 && !isSleeping) { 
    if ((valCpuTemp >= maxCpuTemp && valCpuTemp > 0) || (valGpuTemp >= maxGpuTemp && valGpuTemp > 0)) {
      playAlarmSound(); lastAlarmTime = millis();
    }
  }
}

// --- ΝΕΟ: WAKE ON LAN (Ενεργοποιεί το PC!) ---
void wakeMyPC() {
  displayLarge.clearDisplay(); displayLarge.setCursor(10, 25); displayLarge.print("Sending Magic Packet..."); displayLarge.display();
  
  byte mac[6];
  sscanf(PC_MAC_ADDRESS, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  
  byte magicPacket[102];
  for (int i = 0; i < 6; i++) magicPacket[i] = 0xFF; // Πρώτα 6 bytes = FF
  for (int i = 1; i <= 16; i++) { // Επανάληψη MAC 16 φορές
    for (int j = 0; j < 6; j++) magicPacket[i * 6 + j] = mac[j];
  }
  
  // Στέλνει το πακέτο Broadcast στο ρούτερ
  IPAddress broadcastIp(255, 255, 255, 255);
  udp.beginPacket(broadcastIp, 9);
  udp.write(magicPacket, sizeof(magicPacket));
  udp.endPacket();
  
  playSuccessSound();
  delay(1000);
}

// --- ΝΕΟ: ΗΧΗΤΙΚΑ ΕΦΕ (Μουσική) ---
void playTone(int freq, int duration_ms) {
  int period = 1000000 / freq; int half_period = period / 2; long cycles = ((long)freq * duration_ms) / 1000;
  for (long i=0; i < cycles; i++) { digitalWrite(BUZZER_PIN, HIGH); delayMicroseconds(half_period); digitalWrite(BUZZER_PIN, LOW); delayMicroseconds(half_period); }
}

void playBootSound() {
  playTone(523, 100); delay(50); // C5
  playTone(659, 100); delay(50); // E5
  playTone(784, 150);            // G5
}

void playSuccessSound() {
  playTone(988, 100); delay(30); // B5
  playTone(1319, 200);           // E6 (Mario Coin Effect!)
}

void playAlarmSound() {
  playTone(2000, 200); delay(50);
  playTone(1500, 200); delay(50);
  playTone(2000, 200);
}

// --- ΝΕΟ: ΠΡΟΣΤΑΣΙΑ ΟΘΟΝΗΣ (OLED SCREENSAVER) ---
void drawScreensaver() {
  displaySmall.clearDisplay(); displaySmall.display(); // Η μικρή οθόνη σβήνει τελείως
  
  displayLarge.clearDisplay();
  displayLarge.setTextSize(1);
  displayLarge.setCursor(saverX, saverY);
  displayLarge.print("ZzZ...");
  
  saverX += saverDX; saverY += saverDY;
  if (saverX <= 0 || saverX >= 128 - 36) saverDX = -saverDX; // "Χτυπάει" στους τοίχους
  if (saverY <= 0 || saverY >= 64 - 8) saverDY = -saverDY;
  
  displayLarge.display();
}

void processT9Timer() {
  if (t9_pendingChar != 0 && (millis() - t9_lastTapTime > 1000)) {
    t9_buffer += t9_pendingChar; t9_pendingChar = 0;
    if (t9_capsMode == 1) t9_capsMode = 0; 
    updateDisplay();
  }
}

void writeEEPROMString(int startAddr, String data) {
  int len = data.length(); EEPROM.write(startAddr, len);
  for (int i = 0; i < len; i++) { EEPROM.write(startAddr + 1 + i, data[i]); }
  EEPROM.commit();
}

String readEEPROMString(int startAddr) {
  int len = EEPROM.read(startAddr);
  if (len == 0 || len > 64) return ""; 
  String data = "";
  for (int i = 0; i < len; i++) { data += char(EEPROM.read(startAddr + 1 + i)); }
  return data;
}

void connectWiFi() {
  displayLarge.clearDisplay(); displayLarge.setTextColor(SH110X_WHITE); displayLarge.setTextSize(1);
  displayLarge.setCursor(0, 10); displayLarge.print("Connecting to:");
  displayLarge.setCursor(0, 25); displayLarge.print(mySSID.length() > 0 ? mySSID : "No Saved SSID");
  displayLarge.display();

  if (mySSID.length() > 0) {
    WiFi.begin(mySSID.c_str(), myPASS.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); attempts++; }
    
    if (WiFi.status() == WL_CONNECTED) {
      udp.begin(localUdpPort);
      displayLarge.clearDisplay();
      displayLarge.setCursor(0, 10); displayLarge.print("Wi-Fi Connected!");
      displayLarge.setCursor(0, 30); displayLarge.setTextSize(2); displayLarge.print(WiFi.localIP()); 
      displayLarge.display();
      playSuccessSound(); delay(4000); 
    }
  }
}

void updateDisplay() { drawSmallScreen(); drawLargeScreen(); }

void drawGraph(String title, int currentValue, int* historyArray) {
  displayLarge.setTextSize(1); displayLarge.setCursor(0, 0);
  displayLarge.print(title); displayLarge.print(" ");
  displayLarge.print(currentValue); displayLarge.println("%");
  displayLarge.drawRect(0, 15, 128, 49, SH110X_WHITE);
  for (int i = 0; i < HISTORY_SIZE - 1; i++) {
    int x0 = map(i, 0, HISTORY_SIZE - 1, 1, 126); int x1 = map(i + 1, 0, HISTORY_SIZE - 1, 1, 126);
    int y0 = map(historyArray[i], 0, 100, 62, 16); int y1 = map(historyArray[i+1], 0, 100, 62, 16);
    displayLarge.drawLine(x0, y0, x1, y1, SH110X_WHITE);
  }
}

void drawSmallScreen() {
  displaySmall.clearDisplay(); displaySmall.setTextColor(WHITE);
  if (currentApp == T9_INPUT) {
    displaySmall.setTextSize(1); displaySmall.setCursor(0, 0);
    displaySmall.println("1:.,?!1  2:abc2"); displaySmall.println("3:def3   4:ghi4"); displaySmall.println("5:jkl5   6:mno6");
    displaySmall.println("7:pqrs7  8:tuv8"); displaySmall.println("9:wxyz9  0:Space"); displaySmall.println("*:Home   #:Caps"); displaySmall.println("A:Del    D:Save");
  }
  else if (currentApp == MAIN_MENU || currentApp == SETTINGS_MENU || currentApp == WIFI_MENU || currentApp == NETWORK_INFO) { 
    displaySmall.setTextSize(2); displaySmall.setCursor(15, 20); displaySmall.print("MAIN OS"); 
  } 
  else {
    displaySmall.setTextSize(1); displaySmall.setCursor(0, 8); displaySmall.print("CPU"); 
    displaySmall.setCursor(0, 30); displaySmall.print("GPU"); displaySmall.setCursor(0, 52); displaySmall.print("RAM");
    displaySmall.setTextSize(2);
    displaySmall.setCursor(24, 2);  displaySmall.printf("%d%% %dC", valCpuPct, valCpuTemp);
    displaySmall.setCursor(24, 24); displaySmall.printf("%d%% %dC", valGpuPct, valGpuTemp);
    displaySmall.setCursor(24, 46); displaySmall.printf("%d%%", valRamPct);
  }
  displaySmall.display();
}

void drawLargeScreen() {
  displayLarge.clearDisplay(); displayLarge.setTextColor(SH110X_WHITE, SH110X_BLACK); displayLarge.setTextSize(1);
  
  if (currentApp == MAIN_MENU) {
    displayLarge.setCursor(0, 0); displayLarge.print("--- APP MENU ---"); displayLarge.drawLine(0, 10, 128, 10, SH110X_WHITE);
    displayLarge.drawRect(124, 13, 4, 51, SH110X_WHITE); 
    int scrollKnobHeight = 51 / TOTAL_MENU_ITEMS; int scrollKnobY = 13 + (menuSelection * scrollKnobHeight);
    if(menuSelection == TOTAL_MENU_ITEMS - 1) scrollKnobY = 64 - scrollKnobHeight; 
    displayLarge.fillRect(124, scrollKnobY, 4, scrollKnobHeight, SH110X_WHITE);
    for (int i = 0; i < 4; i++) {
      int itemIndex = menuTopIndex + i; if (itemIndex >= TOTAL_MENU_ITEMS) break;
      int yPos = 14 + (i * 12); displayLarge.setCursor(0, yPos);
      if (itemIndex == menuSelection) displayLarge.print("> "); else displayLarge.print("  ");
      displayLarge.print(itemIndex + 1); displayLarge.print(". "); displayLarge.print(menuItems[itemIndex]);
    }
  } 
  else if (currentApp == SETTINGS_MENU) {
    displayLarge.setCursor(0, 0); displayLarge.print("-- SETTINGS --"); displayLarge.drawLine(0, 10, 128, 10, SH110X_WHITE);
    displayLarge.setCursor(0, 20); if(settingsSelection==0) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("1. Wi-Fi Setup");
    displayLarge.setCursor(0, 35); if(settingsSelection==1) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("2. Network Info");
    displayLarge.setCursor(0, 50); if(settingsSelection==2) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("3. Turn ON PC (WOL)");
  }
  else if (currentApp == WIFI_MENU) {
    displayLarge.setCursor(0, 0); displayLarge.print("- Wi-Fi Setup -"); displayLarge.drawLine(0, 10, 128, 10, SH110X_WHITE);
    displayLarge.setCursor(0, 20); if(wifiSelection==0) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("1. SSID: "); displayLarge.print(mySSID);
    displayLarge.setCursor(0, 35); if(wifiSelection==1) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("2. Pass: *****");
  }
  else if (currentApp == NETWORK_INFO) {
    displayLarge.setCursor(0, 0); displayLarge.print("- Network Info -"); displayLarge.drawLine(0, 10, 128, 10, SH110X_WHITE);
    displayLarge.setCursor(0, 25);
    if (WiFi.status() == WL_CONNECTED) { displayLarge.print("Status: Connected\nIP: "); displayLarge.print(WiFi.localIP()); } 
    else { displayLarge.print("Status: Disconnected"); }
  }
  else if (currentApp == T9_INPUT) {
    displayLarge.setCursor(0, 0); displayLarge.print(wifiSelection == 0 ? "Enter SSID:" : "Enter Password:"); displayLarge.drawLine(0, 10, 128, 10, SH110X_WHITE);
    String showText = t9_buffer; if (showText.length() > 18) showText = "..." + showText.substring(showText.length() - 15);
    displayLarge.setCursor(0, 25); displayLarge.setTextSize(1); displayLarge.print(showText);
    if (t9_pendingChar != 0 && t9_lastKey >= '0' && t9_lastKey <= '9') {
      int numKey = t9_lastKey - '0'; String mapStr = t9_map[numKey]; int mapLen = mapStr.length(); int currentIdx = t9_tapIndex % mapLen; int startX = 60;
      for (int i = 0; i < mapLen; i++) {
        char c = mapStr[i]; if (t9_capsMode > 0 && c >= 'a' && c <= 'z') c -= 32; 
        displayLarge.setCursor(startX, 52);
        if (i == currentIdx) { displayLarge.setTextColor(SH110X_BLACK, SH110X_WHITE); } else { displayLarge.setTextColor(SH110X_WHITE, SH110X_BLACK); }
        displayLarge.print(c); startX += 8; 
      }
    }
    displayLarge.setTextColor(SH110X_WHITE, SH110X_BLACK); 
    displayLarge.drawRect(100, 48, 28, 16, SH110X_WHITE); displayLarge.setCursor(105, 52);
    if (t9_capsMode == 0) displayLarge.print("abc"); else if (t9_capsMode == 1) displayLarge.print("Abc"); else displayLarge.print("ABC");
  }
  else if (currentApp == PC_MONITOR) {
    if (tempSettingStep > 0) { 
      displayLarge.setTextSize(1); displayLarge.setCursor(20, 0); displayLarge.print("Set Max Temp"); displayLarge.drawLine(0, 10, 128, 10, SH110X_WHITE);
      displayLarge.setTextSize(2);
      displayLarge.setCursor(0, 20); if (tempSettingStep == 1) displayLarge.print(">"); else displayLarge.print(" ");
      displayLarge.print("Cpu:"); displayLarge.print(tempStrCpu); if (tempSettingStep == 1 && isTypingTemp) displayLarge.print("_"); 
      displayLarge.setCursor(0, 44); if (tempSettingStep == 2) displayLarge.print(">"); else displayLarge.print(" ");
      displayLarge.print("Gpu:"); displayLarge.print(tempStrGpu); if (tempSettingStep == 2 && isTypingTemp) displayLarge.print("_");
    } 
    else {
      if (pcView == '1') {
        displayLarge.setCursor(0, 0);  displayLarge.print(cpuName); 
        displayLarge.setCursor(0, 12); float freqGHz = valCpuFreq / 1000.0; 
        if (sessionMinCpuTemp == 999) displayLarge.printf("%.1fGHz L:--c/H:--c", freqGHz); 
        else displayLarge.printf("%.1fGHz L:%dc/H:%dc", freqGHz, sessionMinCpuTemp, sessionMaxCpuTemp);
        displayLarge.drawLine(0, 22, 128, 22, SH110X_WHITE);
        displayLarge.setCursor(0, 26); displayLarge.print(gpuName);
        displayLarge.setCursor(0, 38); displayLarge.printf("VRAM: %.1f/%d GB", valVramUsed, valVramTotal); 
        displayLarge.drawLine(0, 48, 128, 48, SH110X_WHITE);
        displayLarge.setCursor(0, 52); displayLarge.printf("RAM %dMHz %.1f/%dGB", valRamSpeed, valRamUsed, valRamTotal);
      } 
      else if (pcView == 'A') { 
        displayLarge.setCursor(0, 0); displayLarge.print("Cores Usage (28T):"); displayLarge.drawLine(0, 10, 128, 10, SH110X_WHITE);
        int barWidth = 3; int gap = 1; int startX = 8;    
        for(int i = 0; i < 28; i++) {
          int barHeight = map(core_usage[i], 0, 100, 0, 50); 
          int x = startX + i * (barWidth + gap); int y = 64 - barHeight; 
          displayLarge.fillRect(x, y, barWidth, barHeight, SH110X_WHITE);
        }
      }
      else if (pcView == 'B') { drawGraph("CPU Usage:", valCpuPct, cpu_hist); }
      else if (pcView == 'C') { drawGraph("GPU Usage:", valGpuPct, gpu_hist); }
      else if (pcView == 'D') { drawGraph("RAM Usage:", valRamPct, ram_hist); }
    }
  }
  displayLarge.display();
}