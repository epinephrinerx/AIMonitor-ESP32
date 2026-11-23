// AIMonitor display สำหรับ ESP32-2432S028 (CYD 2USB, ILI9341 + XPT2046)
// รับ snapshot โควตาจาก AIMonitor บน PC ผ่าน USB serial (ดู docs/PROTOCOL.md)
// แตะจอ = เปลี่ยนหน้า, หน้าเปลี่ยนเองทุก 10 วินาทีเมื่อมีหลายหน้า
// ปุ่มข้างนาฬิกา (หรือกดค้าง 2 วินาทีตรงไหนก็ได้) = หมุนจอทีละ 90° และจำไว้ใน NVS

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "model.h"
#include "ui.h"

constexpr int TOUCH_CLK = 25, TOUCH_MISO = 39, TOUCH_MOSI = 32, TOUCH_CS = 33, TOUCH_IRQ = 36;
constexpr int LED_R = 4, LED_G = 16, LED_B = 17;  // active LOW

constexpr size_t SERIAL_LINE_MAX = 4096;
constexpr uint32_t STALE_AFTER_SEC = 180;  // PC ส่ง heartbeat อย่างน้อยทุก 60 วิ
constexpr uint32_t PAGE_ROTATE_MS = 10000;
constexpr uint32_t LONG_PRESS_MS = 2000;

// ค่า raw ของ XPT2046 บน CYD (หลัง ts.setRotation แปลงแกนให้ตรงกับจอแล้ว)
constexpr int RAW_MIN = 200, RAW_MAX = 3800;

constexpr uint8_t DEFAULT_ROTATION = 3;  // MADCTL 0xE8 แบบเฟิร์มแวร์เดิม

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;
uint8_t rotation = DEFAULT_ROTATION;

Snapshot snap;
bool hasSnap = false;
uint32_t frameMillis = 0;  // millis() ตอนได้ snapshot ล่าสุด
bool dirty = false;        // มีข้อมูลใหม่ ต้องวาดใหม่ทันที

char line[SERIAL_LINE_MAX];
size_t lineLen = 0;
bool lineOverflow = false;

void setLed(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

// LED หลังบอร์ด: ดับ = ปกติ, เหลือง = High/Very high, แดง = Critical
void updateLed(bool linkStale) {
  Severity worst = SEV_NORMAL;
  if (hasSnap && !linkStale) {
    for (int i = 0; i < snap.count; i++) {
      if (snap.p[i].stale) continue;
      for (int j = 0; j < snap.p[i].count; j++)
        worst = (Severity)max<uint8_t>(worst, effectiveSeverity(snap.p[i].w[j]));
    }
  }
  if (worst == SEV_CRITICAL) setLed(1, 0, 0);
  else if (worst >= SEV_HIGH) setLed(1, 1, 0);
  else setLed(0, 0, 0);
}

void handleLine(char* s) {
  while (*s == ' ' || *s == '\r') s++;
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
  if (n == 0) return;

  if (strcmp(s, "ping") == 0) {
    Serial.printf("pong aimonitor-cyd %d\n", PROTOCOL_VERSION);
    return;
  }
  if (s[0] != '{') {
    Serial.println("err unknown");
    return;
  }

  static Snapshot incoming;  // ใหญ่ ไม่วางบน stack
  String error;
  if (!parseFrame(s, incoming, error)) {
    Serial.printf("err %s\n", error.c_str());
    return;
  }
  snap = incoming;
  hasSnap = true;
  frameMillis = millis();
  dirty = true;
  Serial.println("ok");
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (lineOverflow) Serial.println("err too_long");
      else {
        line[lineLen] = 0;
        handleLine(line);
      }
      lineLen = 0;
      lineOverflow = false;
    } else if (!lineOverflow) {
      if (lineLen < SERIAL_LINE_MAX - 1) line[lineLen++] = c;
      else lineOverflow = true;
    }
  }
}

void applyRotation(uint8_t r) {
  rotation = r & 3;
  tft.setRotation(rotation);
  ts.setRotation(rotation);  // XPT2046 ใช้เลขทิศชุดเดียวกับ TFT_eSPI
  uiOrientationChanged();
}

void rotateScreen() {
  applyRotation(rotation + 1);
  prefs.putUChar("rot", rotation);
  Serial.printf("rotation %u\n", rotation);
}

// พิกัดบนจอของจุดที่แตะ ตามทิศจอปัจจุบัน
void touchPoint(int& x, int& y) {
  TS_Point p = ts.getPoint();
  x = constrain(map(p.x, RAW_MIN, RAW_MAX, 0, tft.width() - 1), 0, tft.width() - 1);
  y = constrain(map(p.y, RAW_MIN, RAW_MAX, 0, tft.height() - 1), 0, tft.height() - 1);
}

void setup() {
  Serial.setRxBufferSize(SERIAL_LINE_MAX);
  Serial.begin(115200);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(0, 0, 0);

  tft.init();
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);

  prefs.begin("aimonitor", false);
  applyRotation(prefs.getUChar("rot", DEFAULT_ROTATION));
  uiBegin();
  Serial.printf("hello aimonitor-cyd %d\n", PROTOCOL_VERSION);
}

void loop() {
  pollSerial();

  static uint32_t lastRender = 0, lastPageTurn = 0;
  static bool wasTouched = false, longPressDone = false;
  static uint32_t pressStart = 0;
  static int pressX = 0, pressY = 0;
  bool redraw = false;

  // จอสัมผัสแบบ resistive อาจหลุดสัมผัสชั่วขณะระหว่างกด: นับว่ายังกดอยู่ถ้าเพิ่งหลุดไม่ถึง 80 ms
  static uint32_t lastContact = 0;
  if (ts.tirqTouched() && ts.touched()) lastContact = millis();
  bool touched = lastContact && millis() - lastContact < 80;
  if (touched && !wasTouched) {
    // เริ่มแตะ: จำตำแหน่งไว้ ตัดสินตอนปล่อยนิ้ว (แยกแตะสั้นกับกดค้าง)
    pressStart = millis();
    longPressDone = false;
    touchPoint(pressX, pressY);
  } else if (touched && !longPressDone && millis() - pressStart >= LONG_PRESS_MS) {
    longPressDone = true;  // กดค้าง = หมุนจอ (สำรองเผื่อแตะปุ่มไม่โดน)
    rotateScreen();
    redraw = true;
  } else if (!touched && wasTouched && !longPressDone) {
    if (uiHitRotateButton(pressX, pressY)) {
      rotateScreen();
    } else {
      uiNextPage();
      lastPageTurn = millis();
    }
    redraw = true;
  }
  wasTouched = touched;

  if (uiPageCount() > 1 && millis() - lastPageTurn >= PAGE_ROTATE_MS) {
    uiNextPage();
    lastPageTurn = millis();
    redraw = true;
  }

  if (redraw || dirty || millis() - lastRender >= 1000) {
    lastRender = millis();
    dirty = false;
    uint32_t since = hasSnap ? (millis() - frameMillis) / 1000 : 0;
    bool linkStale = hasSnap && since >= STALE_AFTER_SEC;
    UiState st{hasSnap ? &snap : nullptr, hasSnap ? snap.now + since : 0, since, linkStale};
    uiRender(st);
    updateLed(linkStale);
  }
}
