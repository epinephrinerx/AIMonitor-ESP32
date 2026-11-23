#pragma once
#include <Arduino.h>

constexpr int PROTOCOL_VERSION = 1;
constexpr int MAX_PROVIDERS = 6;
constexpr int MAX_WINDOWS = 8;

// ตรงกับ AIMonitor.Domain.Severity
enum Severity : uint8_t { SEV_NORMAL = 0, SEV_HIGH = 1, SEV_VERY_HIGH = 2, SEV_CRITICAL = 3 };

struct QuotaWindow {
  char title[24];
  bool hasPct;
  float pct;
  uint8_t sev;      // severity ที่ PC ส่งมา
  uint32_t reset;   // unix time, 0 = ไม่ทราบ
  char lock[32];
};

struct Provider {
  char id[16];
  char name[20];
  char err[48];
  bool stale;
  uint8_t count;
  QuotaWindow w[MAX_WINDOWS];
};

struct Snapshot {
  uint32_t now;
  int32_t tzMin;
  uint8_t count;
  Provider p[MAX_PROVIDERS];
};

// แปลงบรรทัด JSON เป็น Snapshot; คืน false พร้อมเหตุผลใน error
bool parseFrame(const char* line, Snapshot& out, String& error);

// เหมือน SeverityRules.Combine: เลือกค่าที่แย่กว่าระหว่างเกณฑ์ local (75/90) กับของ server
Severity effectiveSeverity(const QuotaWindow& w);
