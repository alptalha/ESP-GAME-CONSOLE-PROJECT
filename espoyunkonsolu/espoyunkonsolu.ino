/*
  ESP Game Controller - Config-only Device Status (fixed)
  - ST7735 TFT
  - Joystick (Y) + BTN_OK + BTN_BACK + BTN_3 (refresh Device Status when in CONFIG)
  - Main menu: GAME / CONFIG
  - CONFIG -> Device Status (Flash, Heap, CPU temp, Program size)
  - NTP time sync (if WiFi connects)
  - Minimal redraw logic to avoid flicker
*/

#include <SPI.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "time.h"

// ---------------- HARDWARE PINS ----------------
#define TFT_CS     16
#define TFT_DC     17
#define TFT_RST    5
#define TFT_SCK    25
#define TFT_MOSI   26

#define LED_OK     21
#define LED_ERR    15

#define JOY_X      34
#define JOY_Y      35
#define BTN_OK     14
#define BTN_BACK   27
#define BTN_3      13  // special: refresh Device Status when in CONFIG
#ifndef DARKGREY
#define DARKGREY 0x7BEF
#endif

#ifndef ST77XX_ROAD
#define ST77XX_ROAD ST77XX_BLACK
#endif

#ifndef ST77XX_ORANGE
#define ST77XX_ORANGE 0xFD20
#endif

// TFT object
SPIClass spiTFT(VSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);

// WiFi / NTP
const char* ssid     = ""; // !!
const char* password = ""; // !!
const char* ntpServer = "pool.ntp.org";

// Timekeeping
static int lastSec = -1;
unsigned long lastMillis = 0;
struct tm currentTime;

// UI / menus
enum TopMode { MODE_MAIN, MODE_GAME, MODE_CONFIG };
TopMode topMode = MODE_MAIN;

enum ConfigSub { CFG_DEVICE_STATUS = 1 };
bool inSubMenu = false;
ConfigSub cfgState = CFG_DEVICE_STATUS;

// menu indices & state for minimal redraw
int mainIdx = 0;    // 0: GAME, 1: CONFIG
int lastMainIdx = -1;
ConfigSub lastCfgState = CFG_DEVICE_STATUS;

// joystick & debounce
#define DEBOUNCE_DELAY 220
unsigned long lastMoveTime = 0;

// BTN_3 debounce
bool lastBtn3State = HIGH;
unsigned long lastBtn3Time = 0;
#define BTN3_DEBOUNCE 250

// timing for periodic screen update
unsigned long lastScreenUpdate = 0;
const unsigned long SCREEN_UPDATE_INTERVAL = 500; // ms

// helper forward declarations
void drawClockAndDate();
void drawMainIfChanged();
void drawConfigIfChanged();
void showLoading();
void updateInternalClock();
void attemptWiFiAndNTP();
void readInputs();
void handleButtons();
void showDeviceStatus();
// global değişkenler

// ----------------- TIME & CLOCK -----------------
void updateInternalClock() {
  unsigned long now = millis();
  if (now - lastMillis >= 1000) {
    lastMillis += 1000;
    currentTime.tm_sec++;
    if (currentTime.tm_sec >= 60) {
      currentTime.tm_sec = 0;
      currentTime.tm_min++;
      if (currentTime.tm_min >= 60) {
        currentTime.tm_min = 0;
        currentTime.tm_hour++;
        if (currentTime.tm_hour >= 24) currentTime.tm_hour = 0;
      }
    }
  }
}

void printRightAligned(const String &s, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(1);
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  int x = tft.width() - w - 2;
  tft.setCursor(x, y);
  tft.print(s);
}

void drawClockAndDate() {
  char buf1[9], buf2[12];
  sprintf(buf1, "%02d:%02d", currentTime.tm_hour, currentTime.tm_min);
  sprintf(buf2, "%02d/%02d/%04d", currentTime.tm_mday, currentTime.tm_mon + 1, currentTime.tm_year + 1900);

  // top bar
  tft.fillRect(0, 0, tft.width(), 16, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 2);
  tft.print(buf1);
  printRightAligned(String(buf2), 2);
}

// ----------------- LOADING -----------------
void showLoading() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 40);
  tft.print("Loading...");

  int barW = min(120, tft.width() - 20);
  int barX = (tft.width() - barW) / 2;
  int barY = 100;
  int barH = 10;

  tft.drawRect(barX, barY, barW, barH, ST77XX_WHITE);
  for (int i = 0; i <= 100; i++) {
    int fill = (barW * i) / 100;
    tft.fillRect(barX + 1, barY + 1, max(0, fill - 2), barH - 2, ST77XX_GREEN);
    delay(8);
  }
}

// ----------------- WiFi & NTP -----------------
void attemptWiFiAndNTP() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    if (WiFi.status() == WL_CONNECTED) { ok = true; break; }
    digitalWrite(LED_ERR, (millis() / 250) % 2);
    delay(80);
  }
  digitalWrite(LED_ERR, LOW);
  if (ok) {
    digitalWrite(LED_OK, HIGH);
    configTime(3 * 3600, 0, ntpServer);
    if (getLocalTime(&currentTime)) {
      Serial.println("NTP got time");
    } else {
      Serial.println("NTP failed");
    }
  } else {
    Serial.println("WiFi connect failed");
  }
}

// ----------------- Drawing: Main -----------------
void drawMainIfChanged() {
  if (mainIdx == lastMainIdx && !inSubMenu) return;
  tft.fillScreen(ST77XX_BLACK);
  drawClockAndDate();

  tft.setTextSize(3);
  for (int i = 0; i < 2; i++) {
    int y = 40 + i * 35;
    if (i == mainIdx) {
      // inverted background for selection
      int h = 28;
      tft.fillRect(6, y - 6, tft.width() - 12, h + 6, ST77XX_WHITE);
      tft.setTextColor(ST77XX_BLACK);
      tft.setCursor(15, y);
      tft.print((i == 0) ? "GAME" : "CONFIG");
    } else {
      tft.setTextColor(ST77XX_YELLOW);
      tft.setCursor(15, y);
      tft.print((i == 0) ? "GAME" : "CONFIG");
    }
  }
  lastMainIdx = mainIdx;
}

// ----------------- Config submenu drawing -----------------
void drawConfigIfChanged() {
  if (!inSubMenu) return;

  // sadece değiştiyse yeniden çiz
  if (cfgState == CFG_DEVICE_STATUS && lastCfgState == CFG_DEVICE_STATUS) return;

  tft.fillScreen(ST77XX_BLACK);
  drawClockAndDate();

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(8, 28);
  tft.print("Device Status");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  showDeviceStatus();  // tek sefer çizim

  

  lastCfgState = cfgState;  // bir sonraki draw’da gereksiz tekrar çizme
}



// ----------------- Device Status Screen -----------------
void showDeviceStatus() {
    tft.setTextSize(1);
    int y = 56;

    // Labels ve değerler için renk
    uint16_t labelColor = ST77XX_CYAN;
    uint16_t valueColor = ST77XX_WHITE;

    // Flash usage
    uint32_t sketchBytes = ESP.getSketchSize();
    uint32_t flashBytes = ESP.getFlashChipSize();
    float flashPct = (flashBytes != 0) ? (float)sketchBytes * 100.0f / (float)flashBytes : 0.0f;

    tft.setCursor(8, y);
    tft.setTextColor(labelColor);
    tft.print("Flash: ");
    tft.setTextColor(valueColor);
    tft.printf("%d/%d KB (%.1f%%)", sketchBytes/1024, flashBytes/1024, flashPct);
    y += 16;

    // Heap free
    uint32_t freeHeap = ESP.getFreeHeap();
    tft.setCursor(8, y);
    tft.setTextColor(labelColor);
    tft.print("Heap Free: ");
    tft.setTextColor(valueColor);
    tft.printf("%d KB", freeHeap/1024);
    y += 16;

    // Sketch size
    tft.setCursor(8, y);
    tft.setTextColor(labelColor);
    tft.print("Sketch Size: ");
    tft.setTextColor(valueColor);
    tft.printf("%d KB", sketchBytes/1024);
    y += 16;

    // CPU temperature
    float temp = 0.0f;
#ifdef ARDUINO_ARCH_ESP32
    temp = temperatureRead();
#endif
    tft.setCursor(8, y);
    tft.setTextColor(labelColor);
    tft.print("CPU Temp: ");
    tft.setTextColor(valueColor);
    tft.printf("%.1f C", temp);
    y += 20;

    // Hint
    tft.setCursor(8, y);
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("Press BTN_3 to refresh");
}


// ----------------- Input reading -----------------
void readInputs() {
  int y = analogRead(JOY_Y); // 0..4095 range usually
  unsigned long now = millis();

  // joystick moved up/down with debounce (move main menu or do nothing in CONFIG)
  if (now - lastMoveTime > DEBOUNCE_DELAY) {
    if (y < 1000) { // up
      if (!inSubMenu) {
        mainIdx--;
        if (mainIdx < 0) mainIdx = 1;
        drawMainIfChanged();
      } else {
        // in CONFIG: nothing to scroll
      }
      lastMoveTime = now;
    } else if (y > 3000) { // down
      if (!inSubMenu) {
        mainIdx++;
        if (mainIdx > 1) mainIdx = 0;
        drawMainIfChanged();
      } else {
        // in CONFIG: nothing to scroll
      }
      lastMoveTime = now;
    }
  }

  // BTN_3 handling: refresh Device Status when in CONFIG
  int btn3 = digitalRead(BTN_3);
  if (btn3 != lastBtn3State) lastBtn3Time = now;
  if ((now - lastBtn3Time) > BTN3_DEBOUNCE) {
    if (btn3 == LOW && lastBtn3State == HIGH) {
      // pressed event
      if (inSubMenu && cfgState == CFG_DEVICE_STATUS) {
        // force redraw / refresh
        lastCfgState = (ConfigSub)0; // force redraw next call
        drawConfigIfChanged();
      }
    }
  }
  lastBtn3State = btn3;
}
// önden bildir
void handleButtons();


const int NUM_GAMES = 3;
String gameNames[NUM_GAMES] = { "Test Game 1", "Test Game 2", "Test Game 3" };
int selectedGame = 0;

// oyun menüsünü çiz
void showGameMenu() {
  tft.fillScreen(ST77XX_BLACK);
  drawClockAndDate();
  tft.setTextSize(2);
  for (int i = 0; i < NUM_GAMES; i++) {
    if (i == selectedGame) tft.setTextColor(ST77XX_GREEN);
    else tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 40 + i * 30);
    tft.print("> " + gameNames[i]);
  }
}

// oyunları çalıştıracak placeholder fonksiyonlar
// ----------------- Placeholder Oyunlar -----------------


#define MAP_SIZE 8
#define CELL_SIZE 15
#define JOY_THRESHOLD_LOW 1000
#define JOY_THRESHOLD_HIGH 3000

#define TANK_MOVE_DELAY 150
#define BULLET_MOVE_DELAY 50
#define FIRE_DEBOUNCE 200

int gameMap[MAP_SIZE][MAP_SIZE] = {
  {1,1,1,1,1,1,1,1},
  {1,0,0,1,0,0,0,1},
  {1,0,1,1,0,1,0,1},
  {1,0,0,0,0,1,0,1},
  {1,1,0,1,0,0,0,1},
  {1,0,0,0,1,1,0,1},
  {1,0,1,0,0,0,0,1},
  {1,1,1,1,1,1,1,1}
};

int tankX = 1, tankY = 1, tankDir = 0;
int prevTankX = 1, prevTankY = 1;

bool bulletActive = false;
int bulletX, bulletY, bulletDir;
bool prevBulletActive = false;
int prevBulletX, prevBulletY;

unsigned long lastTankMove = 0;
unsigned long lastBulletMove = 0;
unsigned long lastFireTime = 0;

void drawGame() {
    if (prevTankX != tankX || prevTankY != tankY) {
         tft.fillRect(prevTankX * CELL_SIZE, prevTankY * CELL_SIZE, CELL_SIZE, CELL_SIZE, ST77XX_BLACK);
    }
    
    if (prevBulletActive) {
        tft.fillRect(prevBulletX * CELL_SIZE + CELL_SIZE / 4, prevBulletY * CELL_SIZE + CELL_SIZE / 4, CELL_SIZE / 2, CELL_SIZE / 2, ST77XX_BLACK);
    }

    tft.fillRect(tankX * CELL_SIZE, tankY * CELL_SIZE, CELL_SIZE, CELL_SIZE, ST77XX_GREEN);

    if(bulletActive)
        tft.fillRect(bulletX * CELL_SIZE + CELL_SIZE / 4, bulletY * CELL_SIZE + CELL_SIZE / 4, CELL_SIZE / 2, CELL_SIZE / 2, ST77XX_RED);

    prevTankX = tankX;
    prevTankY = tankY;
    prevBulletActive = bulletActive;
    if (bulletActive) {
        prevBulletX = bulletX;
        prevBulletY = bulletY;
    }
}

void readJoystickAndFire() {
    int joyY = analogRead(JOY_Y);
    int joyX = analogRead(JOY_X);

    int newX = tankX;
    int newY = tankY;
    bool wantsToMove = false;

    if (joyY < JOY_THRESHOLD_LOW) { newY--; tankDir = 0; wantsToMove = true; }
    else if (joyY > JOY_THRESHOLD_HIGH) { newY++; tankDir = 2; wantsToMove = true; }
    else if (joyX < JOY_THRESHOLD_LOW) { newX--; tankDir = 3; wantsToMove = true; }
    else if (joyX > JOY_THRESHOLD_HIGH) { newX++; tankDir = 1; wantsToMove = true; }

    if (wantsToMove) {
        if(newX >= 0 && newX < MAP_SIZE && newY >=0 && newY < MAP_SIZE && gameMap[newY][newX] == 0){
            tankX = newX;
            tankY = newY;
        }
    }

    bool firePressed = (digitalRead(BTN_3) == LOW);
    unsigned long now = millis();
    if(firePressed && !bulletActive && (now - lastFireTime > FIRE_DEBOUNCE)){
        lastFireTime = now;
        bulletActive = true;
        bulletX = tankX;
        bulletY = tankY;
        bulletDir = tankDir;
        lastBulletMove = now; 
    }
}

void moveBullet() {
    if(!bulletActive) return;

    switch(bulletDir){
        case 0: bulletY--; break;
        case 1: bulletX++; break;
        case 2: bulletY++; break;
        case 3: bulletX--; break;
    }

    if(bulletX < 0 || bulletX >= MAP_SIZE || bulletY < 0 || bulletY >= MAP_SIZE || gameMap[bulletY][bulletX]==1)
        bulletActive = false;
}

void game1() {
    tankX = 1; tankY = 1; tankDir = 0;
    prevTankX = 1; prevTankY = 1;
    bulletActive = false;
    prevBulletActive = false;

    tft.fillScreen(ST77XX_BLACK);
    // drawClockAndDate(); 

    for(int y=0; y<MAP_SIZE; y++){
        for(int x=0; x<MAP_SIZE; x++){
            if(gameMap[y][x]==1){
                tft.fillRect(x*CELL_SIZE, y*CELL_SIZE, CELL_SIZE, CELL_SIZE, ST77XX_WHITE);
            }
        }
    }

    unsigned long now = millis();
    lastTankMove = now;
    lastBulletMove = now;
    lastFireTime = now;

    while(true){
        now = millis();
        
        if (now - lastTankMove > TANK_MOVE_DELAY) {
            readJoystickAndFire();
            lastTankMove = now;
        }

        if (now - lastBulletMove > BULLET_MOVE_DELAY) {
            moveBullet();
            lastBulletMove = now;
        }

        drawGame();

        if(digitalRead(BTN_BACK) == LOW) {
            delay(50);
            break;
        }
    }
}

// Tam oyun: Top-down Tank Shooter (Joystick Y ekseni, kalibrasyon dahil)
void game2() {
    // ---------- AYARLAR / SABİTLER ----------
    const unsigned long FRAME_MS = 20;           // ~50 FPS
    const int CAR_W = 28, CAR_H = 20;
    const int ROAD_MARGIN = 6;
    const int LANE_COUNT = 7;
    const int MAX_ENEMIES = 4;
    const int MAX_PLAYER_BULLETS = 6;
    const int MAX_ENEMY_BULLETS = 10;
    const float PLAYER_MAX_SPEED = 5.0f;
    const float PLAYER_ACCEL = 0.6f;
    const float BASE_ENEMY_SPEED = 1.2f;
    const unsigned long ENEMY_FIRE_INTERVAL_MIN = 900;
    const unsigned long ENEMY_FIRE_INTERVAL_MAX = 2200;
    const unsigned long PLAYER_FIRE_COOLDOWN = 220;
    const float DIFF_GROW_PER_SEC = 0.03f;
    const int SPAWN_MIN_GAP_PIXELS = CAR_H + 24;
    const float DEADZONE_RATIO = 0.15f;

    // ---------- PINLER ----------
    const int JOY_AXIS = JOY_Y;
    pinMode(BTN_3, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);

    // ---------- EKRAN ÖLÇÜLERİ ----------
    int scrW = tft.width();
    int scrH = tft.height();
    int roadX = ROAD_MARGIN;
    int roadW = scrW - 2 * ROAD_MARGIN;
    if (LANE_COUNT < 3) {
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextColor(ST77XX_RED);
        tft.setCursor(4,4);
        tft.setTextSize(1);
        tft.print("LANE_COUNT >= 3 gerekli");
        delay(1200);
        return;
    }
    float laneW = (float)roadW / (float)LANE_COUNT;

    // ---------- JOYSTICK KALİBRASYONU ----------
    struct JoystickCalib { int minVal, maxVal, center; } calib;

    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10,10);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.print("Joystick'i ortada birak...");
    delay(2000);
    calib.center = analogRead(JOY_AXIS);

    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10,10);
    tft.print("Joystick'i sola/sağa cek...");
    int minVal = 4095, maxVal = 0;
    unsigned long start = millis();
    while (millis() - start < 3000) {
        int val = analogRead(JOY_AXIS);
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
        delay(10);
    }
    calib.minVal = minVal;
    calib.maxVal = maxVal;

    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(10,10);
    tft.print("Kalibrasyon tamam, oyun basliyor...");
    delay(1000);

    // ---------- PLAYER ----------
    float playerX = roadX + (roadW - CAR_W)/2.0f;
    int playerY = scrH - CAR_H - 8;
    float playerVel = 0.0f;
    unsigned long lastPlayerFire = 0;

    struct Bullet { float x, y; float vy; bool active; };
    struct Enemy { float x, y; float speed; bool active; unsigned long nextFireAt; int lane; };

    Bullet playerBullets[MAX_PLAYER_BULLETS];
    Bullet enemyBullets[MAX_ENEMY_BULLETS];
    Enemy enemies[MAX_ENEMIES];

    for (int i=0;i<MAX_PLAYER_BULLETS;i++) playerBullets[i].active = false;
    for (int i=0;i<MAX_ENEMY_BULLETS;i++)  enemyBullets[i].active = false;
    for (int i=0;i<MAX_ENEMIES;i++)        enemies[i].active = false;

    // ---------- GRAFİK YARDIMCILARI ----------
    auto drawBackground = [&]() {
        tft.fillRect(0,0,ROAD_MARGIN,scrH,DARKGREY);
        tft.fillRect(scrW-ROAD_MARGIN,0,ROAD_MARGIN,scrH,DARKGREY);
        tft.fillRect(roadX,0,roadW,scrH,ST77XX_ROAD);
        int dashH=10,dashGap=10;
        int centerXdash = roadX+roadW/2;
        for(int y=0;y<scrH;y+=dashH+dashGap) tft.fillRect(centerXdash-2,y,4,dashH,ST77XX_WHITE);
    };

    auto drawTank = [&](int x,int y,uint16_t color){
        tft.fillRect(x,y,CAR_W,CAR_H,color);
        tft.fillRect(x+CAR_W/2-2,y-6,4,6,color);
        tft.fillRect(x+3,y+4,6,5,ST77XX_BLACK);
        tft.fillRect(x+CAR_W-9,y+4,6,5,ST77XX_BLACK);
    };

    auto drawEnemyRect = [&](int x,int y){
        tft.fillRect(x,y,CAR_W,CAR_H,ST77XX_RED);
        tft.fillRect(x+3,y+6,CAR_W-6,6,ST77XX_BLACK);
    };

    auto drawBullet = [&](int x,int y,uint16_t color){
        tft.fillRect(x-2,y-4,4,8,color);
    };

    auto eraseRect = [&](int x,int y,int w,int h){
        if(w<=0||h<=0)return;
        int rx=x,ry=y,rw=w,rh=h;
        if(rx<0){rw+=rx;rx=0;}
        if(ry<0){rh+=ry;ry=0;}
        if(rx>=scrW||ry>=scrH)return;
        if(rx+rw>scrW) rw=scrW-rx;
        if(ry+rh>scrH) rh=scrH-ry;
        if(rw<=0||rh<=0) return;
        if(rx+rw<roadX||rx>roadX+roadW) tft.fillRect(rx,ry,rw,rh,ST77XX_BLACK);
        else tft.fillRect(rx,ry,rw,rh,ST77XX_ROAD);
    };

    // ---------- SPAWN ---------- 
    auto spawnEnemiesWave = [&](float difficulty){
        int laneIndices[LANE_COUNT]; for(int i=0;i<LANE_COUNT;i++) laneIndices[i]=i;
        for(int i=LANE_COUNT-1;i>0;i--){ int j=random(0,i+1); int tmp=laneIndices[i]; laneIndices[i]=laneIndices[j]; laneIndices[j]=tmp; }
        int gapLane=random(0,LANE_COUNT);
        int avail[LANE_COUNT]; int ac=0;
        for(int i=0;i<LANE_COUNT;i++) if(i!=gapLane) avail[ac++]=i;
        for(int i=ac-1;i>0;i--){ int j=random(0,i+1); int tmp=avail[i]; avail[i]=avail[j]; avail[j]=tmp; }
        int spawned=0;
        for(int i=0;i<ac&&spawned<MAX_ENEMIES;i++){
            int lane=avail[i]; int idx=-1;
            for(int e=0;e<MAX_ENEMIES;e++) if(!enemies[e].active){idx=e;break;}
            if(idx<0) break;
            enemies[idx].x=roadX+lane*laneW+(laneW-CAR_W)/2.0f;
            enemies[idx].y=-(spawned*(CAR_H+8)+8);
            enemies[idx].speed=BASE_ENEMY_SPEED+random(0,50)/50.0f+difficulty*0.12f;
            enemies[idx].active=true;
            enemies[idx].lane=lane;
            enemies[idx].nextFireAt=millis()+random((int)ENEMY_FIRE_INTERVAL_MIN,(int)ENEMY_FIRE_INTERVAL_MAX);
            spawned++;
        }
    };

    // ---------- INIT ----------
    drawBackground();
    spawnEnemiesWave(1.0f);
    int prevPlayerX=(int)playerX, prevPlayerY=playerY;
    int prevEnemyX[MAX_ENEMIES], prevEnemyY[MAX_ENEMIES]; for(int i=0;i<MAX_ENEMIES;i++){prevEnemyX[i]=-100;prevEnemyY[i]=-100;}
    int prevPlayerBulletX[MAX_PLAYER_BULLETS], prevPlayerBulletY[MAX_PLAYER_BULLETS]; for(int i=0;i<MAX_PLAYER_BULLETS;i++){prevPlayerBulletX[i]=-100;prevPlayerBulletY[i]=-100;}
    int prevEnemyBulletX[MAX_ENEMY_BULLETS], prevEnemyBulletY[MAX_ENEMY_BULLETS]; for(int i=0;i<MAX_ENEMY_BULLETS;i++){prevEnemyBulletX[i]=-100;prevEnemyBulletY[i]=-100;}

    bool isGameOver=false;
    float lastN=0.0f;
    unsigned long lastFrame=millis(), startTime=millis(), lastScoreTick=millis();
    unsigned long score=0;

    // ---------- OYUN DÖNGÜSÜ ----------
    while(true){
        unsigned long now=millis();
        if(now-lastFrame<FRAME_MS){delay(1);continue;}
        lastFrame=now;
        float difficulty=1.0f+((now-startTime)/1000.0f)*DIFF_GROW_PER_SEC;

        // ---------- JOYSTICK OKU ----------
        int raw=analogRead(JOY_AXIS);
        if(raw<calib.minVal) raw=calib.minVal;
        if(raw>calib.maxVal) raw=calib.maxVal;
        float n;
        if(raw>=calib.center) n=(float)(raw-calib.center)/(float)(calib.maxVal-calib.center);
        else n=(float)(raw-calib.center)/(float)(calib.center-calib.minVal);
        n=constrain(n,-1.0f,1.0f);
        if(abs(n)<DEADZONE_RATIO) n=0.0f;
        lastN=lastN*0.6f+n*0.4f;
        float smoothN=lastN;

        // ---------- PLAYER HAREKET ----------
        float targetVel=smoothN*PLAYER_MAX_SPEED;
        if(playerVel<targetVel){playerVel+=PLAYER_ACCEL;if(playerVel>targetVel)playerVel=targetVel;}
        else if(playerVel>targetVel){playerVel-=PLAYER_ACCEL;if(playerVel<targetVel)playerVel=targetVel;}
        float oldPlayerXf=playerX;
        playerX+=playerVel;
        if(playerX<roadX) playerX=roadX;
        if(playerX>roadX+roadW-CAR_W) playerX=roadX+roadW-CAR_W;
        if((int)oldPlayerXf!=(int)playerX) eraseRect(prevPlayerX,prevPlayerY-6,CAR_W,CAR_H+6);

        // ---------- PLAYER ATEŞ ----------
        if(digitalRead(BTN_3)==LOW && now-lastPlayerFire>=PLAYER_FIRE_COOLDOWN && !isGameOver){
            int bIdx=-1;
            for(int b=0;b<MAX_PLAYER_BULLETS;b++) if(!playerBullets[b].active){bIdx=b;break;}
            if(bIdx>=0){
                playerBullets[bIdx].active=true;
                playerBullets[bIdx].x=playerX+CAR_W/2;
                playerBullets[bIdx].y=playerY-6;
                playerBullets[bIdx].vy=-6.5f;
                prevPlayerBulletX[bIdx]=-100; prevPlayerBulletY[bIdx]=-100;
                lastPlayerFire=now;
            }
        }

        // ---------- ENEMY DAVRANIŞ ----------
        float speedMultiplier=1.0f;
        int maxEnemyY=-10000;
        for(int e=0;e<MAX_ENEMIES;e++){
            if(!enemies[e].active) continue;
            if(prevEnemyY[e]>=-1000) eraseRect(prevEnemyX[e],prevEnemyY[e],CAR_W,CAR_H+4);
            enemies[e].y+=enemies[e].speed*speedMultiplier;
            drawEnemyRect((int)enemies[e].x,(int)enemies[e].y);
            prevEnemyX[e]=(int)enemies[e].x; prevEnemyY[e]=(int)enemies[e].y;
            if((int)enemies[e].y>maxEnemyY) maxEnemyY=(int)enemies[e].y;
            if(enemies[e].y>scrH){enemies[e].active=false; eraseRect(prevEnemyX[e],prevEnemyY[e],CAR_W,CAR_H+4);}
            else if((long)now>=(long)enemies[e].nextFireAt){
                int eb=-1; for(int b=0;b<MAX_ENEMY_BULLETS;b++) if(!enemyBullets[b].active){eb=b;break;}
                if(eb>=0){
                    enemyBullets[eb].active=true;
                    enemyBullets[eb].x=enemies[e].x+CAR_W/2;
                    enemyBullets[eb].y=enemies[e].y+CAR_H+4;
                    enemyBullets[eb].vy=3.0f+difficulty*0.6f;
                    prevEnemyBulletX[eb]=-100; prevEnemyBulletY[eb]=-100;
                }
                enemies[e].nextFireAt=now+random((int)ENEMY_FIRE_INTERVAL_MIN,(int)ENEMY_FIRE_INTERVAL_MAX);
            }
        }

        // ---------- PLAYER BULLETS ----------
        for(int b=0;b<MAX_PLAYER_BULLETS;b++){
            if(!playerBullets[b].active) continue;
            if(prevPlayerBulletY[b]>=0) eraseRect(prevPlayerBulletX[b]-3,prevPlayerBulletY[b]-6,6,12);
            playerBullets[b].y+=playerBullets[b].vy;
            drawBullet((int)playerBullets[b].x,(int)playerBullets[b].y,ST77XX_YELLOW);
            prevPlayerBulletX[b]=(int)playerBullets[b].x; prevPlayerBulletY[b]=(int)playerBullets[b].y;
            if(playerBullets[b].y<-12){playerBullets[b].active=false; eraseRect(prevPlayerBulletX[b]-3,prevPlayerBulletY[b]-6,6,12);}
        }

        // ---------- ENEMY BULLETS ----------
        for(int b=0;b<MAX_ENEMY_BULLETS;b++){
            if(!enemyBullets[b].active) continue;
            if(prevEnemyBulletY[b]>=0) eraseRect(prevEnemyBulletX[b]-3,prevEnemyBulletY[b]-6,6,12);
            enemyBullets[b].y+=enemyBullets[b].vy;
            drawBullet((int)enemyBullets[b].x,(int)enemyBullets[b].y,ST77XX_CYAN);
            prevEnemyBulletX[b]=(int)enemyBullets[b].x; prevEnemyBulletY[b]=(int)enemyBullets[b].y;
            if(enemyBullets[b].y>scrH+12){enemyBullets[b].active=false; eraseRect(prevEnemyBulletX[b]-3,prevEnemyBulletY[b]-6,6,12);}
        }

        // ---------- ÇARPIŞMALAR ----------
        for(int b=0;b<MAX_PLAYER_BULLETS;b++){
            if(!playerBullets[b].active) continue;
            for(int e=0;e<MAX_ENEMIES;e++){
                if(!enemies[e].active) continue;
                int bx=(int)playerBullets[b].x,by=(int)playerBullets[b].y;
                int ex=(int)enemies[e].x,ey=(int)enemies[e].y;
                if(!(bx+2<ex||bx-2>ex+CAR_W||by+4<ey||by-4>ey+CAR_H)){
                    enemies[e].active=false; playerBullets[b].active=false;
                    eraseRect(ex,ey,CAR_W,CAR_H+4);
                    eraseRect(prevPlayerBulletX[b]-3,prevPlayerBulletY[b]-6,6,12);
                    score+=10;
                    tft.fillRect(ex+4,ey+4,CAR_W-8,CAR_H-8,ST77XX_ORANGE);
                    delay(40);
                    eraseRect(ex+4,ey+4,CAR_W-8,CAR_H-8);
                }
            }
        }

        for(int b=0;b<MAX_ENEMY_BULLETS;b++){
            if(!enemyBullets[b].active) continue;
            int bx=(int)enemyBullets[b].x,by=(int)enemyBullets[b].y;
            int px=(int)playerX,py=playerY;
            if(!(bx+2<px||bx-2>px+CAR_W||by+4<py||by-4>py+CAR_H)){
                enemyBullets[b].active=false;
                eraseRect(prevEnemyBulletX[b]-3,prevEnemyBulletY[b]-6,6,12);
                isGameOver=true;
                break;
            }
        }

        // ---------- HUD ----------
        if(now-lastScoreTick>=200){score++; lastScoreTick=now;}
        tft.fillRect(2,2,scrW-4,14,ST77XX_BLACK);
        tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE,ST77XX_BLACK);
        tft.setCursor(4,4); tft.print("PUAN:"); tft.print(score);
        tft.setCursor(scrW-90,4); tft.print("ZORLUK:"); tft.print((int)(difficulty*10));

        // ---------- YENİ DALGA ----------
        bool anyEnemyActive=false; int maxY=-10000;
        for(int e=0;e<MAX_ENEMIES;e++) if(enemies[e].active){anyEnemyActive=true; if((int)enemies[e].y>maxY) maxY=(int)enemies[e].y;}
        if(!anyEnemyActive||maxY>SPAWN_MIN_GAP_PIXELS) spawnEnemiesWave(difficulty);

        // ---------- PLAYER ÇİZ ----------
        int curPlayerX=(int)playerX;
        drawTank(curPlayerX,playerY,ST77XX_GREEN);
        prevPlayerX=curPlayerX; prevPlayerY=playerY;

        // ---------- BUTONLAR / GAMEOVER ----------
        if(digitalRead(BTN_BACK)==LOW){ delay(60); return; }
        if(isGameOver){
            tft.fillRect(0,scrH/2-28,scrW,56,ST77XX_BLACK);
            tft.setTextSize(2); tft.setTextColor(ST77XX_RED,ST77XX_BLACK);
            tft.setCursor(scrW/2-48,scrH/2-8); tft.print("YANDIN!");
            tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE,ST77XX_BLACK);
            tft.setCursor(scrW/2-48,scrH/2+12); tft.print("PUAN:"); tft.print(score);
            unsigned long waitStart=millis();
            while(millis()-waitStart<5000){
                if(digitalRead(BTN_3)==LOW){ delay(100); return game2(); }
                if(digitalRead(BTN_BACK)==LOW){ delay(100); return; }
                delay(40);
            }
            return;
        }
    } // while
} // game2


// ----------------- Oyunları çağıracak fonksiyon -----------------
void runGame(int gameId) {
    switch(gameId) {
        case 0: game1(); break;
        case 1: game2(); break;
        default: break;
    }
}

// global buton durumları (debounce için)
bool lastOkState = HIGH;
bool lastBackState = HIGH;

#define DEBOUNCE_DELAY 200  // joystick hareketleri için gecikme
unsigned long lastJoystickMoveTime = 0;  // joystick menü için zamanlayıcı

// joystick ve oyun menüsü için seçim
void readJoystickMenu() {
    int y = analogRead(JOY_Y);
    unsigned long now = millis();

    if (now - lastJoystickMoveTime > DEBOUNCE_DELAY) {
        if (y < 1000) { // yukarı
            if (topMode == MODE_GAME && inSubMenu) {
                selectedGame++;
                if (selectedGame < 0) selectedGame = NUM_GAMES - 1;
                showGameMenu();
            }
        } else if (y > 3000) { // aşağı
            if (topMode == MODE_GAME && inSubMenu) {
                selectedGame--;
                if (selectedGame >= NUM_GAMES) selectedGame = 0;
                showGameMenu();
            }
        }
        lastJoystickMoveTime = now;
    }
}

// handleButtons() güncel
void handleButtons() {
    bool okPressed = (digitalRead(BTN_OK) == LOW);
    bool backPressed = (digitalRead(BTN_BACK) == LOW);

    if (okPressed && lastOkState == HIGH) {
        if (!inSubMenu) {
            if (mainIdx == 0) {
                topMode = MODE_GAME;
                inSubMenu = true;
                selectedGame = 0;
                showGameMenu();
            } else {
                inSubMenu = true;
                cfgState = CFG_DEVICE_STATUS;
                lastCfgState = (ConfigSub)0;
                drawConfigIfChanged();
            }
        } else {
            if (topMode == MODE_GAME) {
                runGame(selectedGame);
            } else if (cfgState == CFG_DEVICE_STATUS) {
                lastCfgState = (ConfigSub)0;
                drawConfigIfChanged();
            }
        }
    }

    if (backPressed && lastBackState == HIGH) {
        if (inSubMenu) {
            if (topMode == MODE_GAME) {
                // oyun menüsüne dön
                inSubMenu = false;
                drawMainIfChanged();
            } else if (cfgState == CFG_DEVICE_STATUS) {
                inSubMenu = false;
                lastMainIdx = -1;
                drawMainIfChanged();
            }
        }
    }

    lastOkState = okPressed ? LOW : HIGH;
    lastBackState = backPressed ? LOW : HIGH;
}


// ----------------- Setup & Loop -----------------
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(BTN_3, INPUT_PULLUP);
  pinMode(LED_OK, OUTPUT);
  pinMode(LED_ERR, OUTPUT);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  analogReadResolution(12); // 0..4095

  // TFT init
  spiTFT.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB); // ST7735
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);

  showLoading();
  attemptWiFiAndNTP();

  // initial draw
  lastMainIdx = -1;
  drawMainIfChanged();
  lastMillis = millis();
}

void loop() {
  // time
  updateInternalClock();
  if (currentTime.tm_sec != lastSec) {
    lastSec = currentTime.tm_sec;
    drawClockAndDate();
  }

  // inputs and buttons
  readInputs();
  handleButtons();
readJoystickMenu(); // joystick sürekli okunacak
 


  delay(10);
}
