#include "ui.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace {

// สีจาก AIMonitor ThemePalette (Dark) แปลงเป็น RGB565
constexpr uint16_t rgb(uint32_t hex) {
  return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
}
const uint16_t C_PLANE = rgb(0x0d0d0d);
const uint16_t C_SURFACE = rgb(0x1a1a19);
const uint16_t C_INK = rgb(0xffffff);
const uint16_t C_INK2 = rgb(0xc3c2b7);
const uint16_t C_MUTED = rgb(0x898781);
const uint16_t C_TRACK = rgb(0x3a3a37);
const uint16_t C_SEV[4] = {rgb(0x0ca30c), rgb(0xfab219), rgb(0xec835a), rgb(0xd03b3b)};
const char* const SEV_LABEL[4] = {"Normal", "High", "Very high", "Critical"};

// หน้าแบ่งตามชนิดหน้าต่างโควตา: Session ของทุก service, Weekly ของทุก service, ที่เหลือ
enum Category : uint8_t { CAT_SESSION, CAT_WEEKLY, CAT_OTHER, CAT_COUNT };
const char* const CAT_WORD[CAT_COUNT] = {"session", "weekly", ""};
const char* const CAT_TITLE[CAT_COUNT] = {"Session", "Weekly", "Other"};

// ขนาดจอเปลี่ยนตามทิศ: แนวนอน 320x240, แนวตั้ง 240x320 (อัปเดตใน uiOrientationChanged)
int W = 320, H = 240;
constexpr int HEADER_H = 28;
constexpr int BODY_Y = HEADER_H + 2;
int BODY_H = 240 - BODY_Y;

// ใต้วงโค้ง: ชื่อ service (ฟอนต์ 4) + ชื่อย่อยของหน้าต่าง (ฟอนต์ 2, มีเมื่อจำเป็น) + นับถอยหลัง (ฟอนต์ 4)
constexpr int FONT4_H = 26, FONT2_H = 16;
int captionHeight(bool hasSub) { return FONT4_H + (hasSub ? FONT2_H : 0) + FONT4_H + 2; }

// ปุ่มหมุนจอ: อยู่ซ้ายของนาฬิกาในแถบบน
constexpr int CLOCK_W = 36;  // "00:00" ฟอนต์ 2
constexpr int BTN_W = 28, BTN_H = 22;
constexpr int BTN_HIT_SLACK = 10;  // ขยายพื้นที่กดเผื่อ touch คลาดเล็กน้อย
int btnX() { return W - 6 - CLOCK_W - 6 - BTN_W; }
constexpr int BTN_Y = (HEADER_H - BTN_H) / 2;

// รูปทรงเดียวกับ GaugeControl.cs: เริ่ม 140° กวาด 260° (WPF, 0° = 3 นาฬิกา ตามเข็ม)
// TFT_eSPI นับ 0° = 6 นาฬิกา ตามเข็ม → 50° ถึง 310°
constexpr uint32_t ARC_START = 50;
constexpr uint32_t ARC_SWEEP = 260;

// หนึ่ง gauge บนหน้า: หน้าต่าง w ของ service p; w = -1 คือ service ที่ไม่มีข้อมูล (แสดง "--" พร้อมเหตุผล)
struct Item {
  int8_t p, w;
};
constexpr int MAX_ITEMS = 12;

struct Layout {
  int n, cols, rows, cellW, cellH, ring, captionH;
};

TFT_eSprite spr(&tft);
TFT_eSprite hdr(&tft);
int sprW = 0, sprH = 0;
Category pageCat[CAT_COUNT];
int pageCount = 1;
int page = 0;
int lastDrawnKey = -1;  // หน้า + จำนวน gauge + มีบรรทัดย่อยไหม: เปลี่ยนเมื่อไรต้องล้างพื้นที่ทั้งหมด

// หา word ใน s แบบไม่สนตัวพิมพ์ คืนตำแหน่งหรือ nullptr
const char* findNoCase(const char* s, const char* word) {
  size_t n = strlen(word);
  if (n == 0) return nullptr;
  for (; *s; s++)
    if (strncasecmp(s, word, n) == 0) return s;
  return nullptr;
}

Category categoryOf(const QuotaWindow& w) {
  if (findNoCase(w.title, CAT_WORD[CAT_SESSION])) return CAT_SESSION;
  if (findNoCase(w.title, CAT_WORD[CAT_WEEKLY])) return CAT_WEEKLY;
  return CAT_OTHER;
}

bool hasData(const Provider& p) { return !p.err[0] && p.count > 0; }

// หาว่ามีหน้าไหนบ้าง เรียงตาม Session, Weekly, อื่น ๆ
void buildPages(const Snapshot* s) {
  bool present[CAT_COUNT] = {};
  if (s)
    for (int i = 0; i < s->count; i++)
      for (int j = 0; j < s->p[i].count; j++) present[categoryOf(s->p[i].w[j])] = true;
  pageCount = 0;
  for (int c = 0; c < CAT_COUNT; c++)
    if (present[c]) pageCat[pageCount++] = (Category)c;
  if (pageCount == 0) pageCat[pageCount++] = CAT_SESSION;  // ไม่มีหน้าต่างเลย: หน้าเดียวแสดงเหตุผลของแต่ละ service
  if (page >= pageCount) page = 0;
}

void subLabel(const QuotaWindow& w, Category cat, char* out, size_t n);

// gauge ของหน้า: service ละหนึ่งอัน เรียงตาม service; service ที่ไม่มีข้อมูลขึ้นทุกหน้า
// เลือกหน้าต่างหลักของหมวด (ชื่อตรงกับหมวดพอดี เช่น "Weekly" ไม่ใช่ "Weekly Fable") ไม่มีก็ใช้อันแรกของหมวด
int collectItems(const Snapshot& s, Category cat, Item* out) {
  int n = 0;
  for (int i = 0; i < s.count && n < MAX_ITEMS; i++) {
    const Provider& p = s.p[i];
    if (!hasData(p)) {
      out[n++] = {(int8_t)i, -1};
      continue;
    }
    int pick = -1;
    for (int j = 0; j < p.count; j++) {
      if (categoryOf(p.w[j]) != cat) continue;
      char sub[sizeof p.w[j].title];
      subLabel(p.w[j], cat, sub, sizeof sub);
      if (!sub[0]) {
        pick = j;
        break;
      }
      if (pick < 0) pick = j;
    }
    if (pick >= 0) out[n++] = {(int8_t)i, (int8_t)pick};
  }
  return n;
}

// ชื่อ service บน gauge: สั้นกว่าชื่อแท็บในแอป
const char* shortName(const Provider& p) {
  if (strcmp(p.id, "openai") == 0) return "Codex";
  if (strcmp(p.id, "agy") == 0) return "Antigravity";
  return p.name;
}

// ชื่อหน้าต่างหลังตัดคำของหมวดออก: "Weekly Fable" → "Fable", "Gemini Session" → "Gemini", "Session" → ""
void subLabel(const QuotaWindow& w, Category cat, char* out, size_t n) {
  char tmp[sizeof w.title];
  strlcpy(tmp, w.title, sizeof tmp);
  if (const char* hit = findNoCase(tmp, CAT_WORD[cat])) {
    char* at = tmp + (hit - tmp);
    memmove(at, at + strlen(CAT_WORD[cat]), strlen(at + strlen(CAT_WORD[cat])) + 1);
  }
  // ยุบช่องว่างซ้ำ ตัดช่องว่างและ "-" หัวท้าย
  size_t k = 0;
  for (const char* c = tmp; *c && k + 1 < n; c++) {
    if (*c == ' ' && (k == 0 || out[k - 1] == ' ')) continue;
    out[k++] = *c;
  }
  while (k && (out[k - 1] == ' ' || out[k - 1] == '-')) k--;
  out[k] = 0;
  size_t lead = 0;
  while (out[lead] == '-' || out[lead] == ' ') lead++;
  if (lead) memmove(out, out + lead, strlen(out + lead) + 1);
}

// เลือกจำนวนคอลัมน์ที่ให้ gauge ใหญ่ที่สุดในทิศจอปัจจุบัน
Layout computeLayout(int n, bool hasSub) {
  Layout best{};
  const int capH = captionHeight(hasSub);
  for (int cols = 1; cols <= min(n, 4); cols++) {
    Layout L{};
    L.n = n;
    L.cols = cols;
    L.rows = (n + cols - 1) / cols;
    L.cellW = min(160, W / cols);
    L.captionH = capH;
    L.ring = min(L.cellW - 8, BODY_H / L.rows - capH - 2);
    if (L.ring <= best.ring) continue;
    L.cellH = L.ring + capH;
    best = L;
  }
  return best;
}

// ไอคอนลูกศรวน: วงโค้งเกือบรอบ + หัวลูกศรตามเข็มนาฬิกา
void drawRotateIcon(TFT_eSprite& s, int cx, int cy, uint16_t color, uint16_t bg) {
  const int r = 7;
  const uint32_t endAngle = 320;  // TFT_eSPI: 0° = 6 นาฬิกา ตามเข็ม
  s.drawSmoothArc(cx, cy, r, r - 2, 40, endAngle, color, bg, false);
  float a = endAngle * DEG_TO_RAD;
  float px = cx - r * sinf(a) + 1, py = cy + r * cosf(a);
  float dx = -cosf(a), dy = -sinf(a);  // ทิศสัมผัสตามเข็มนาฬิกา
  float nx = -dy, ny = dx;
  s.fillTriangle(px + dx * 4, py + dy * 4, px + nx * 4, py + ny * 4, px - nx * 4, py - ny * 4, color);
}

void ensureSprite(int w, int h) {
  if (w == sprW && h == sprH) return;
  spr.deleteSprite();
  spr.setColorDepth(16);
  if (spr.createSprite(w, h)) {
    sprW = w;
    sprH = h;
  } else {
    sprW = sprH = 0;
  }
}

void fitText(TFT_eSprite& s, char* text, int maxW, uint8_t font) {
  while (strlen(text) > 1 && s.textWidth(text, font) > maxW) text[strlen(text) - 1] = 0;
}

// วาดข้อความกึ่งกลางด้วยฟอนต์ 4 ถ้าพอดีช่อง ไม่พอดีใช้ฟอนต์ 2 (ตัดท้ายถ้ายังยาวเกิน)
void drawFitted(TFT_eSprite& s, const char* text, int cx, int y, int maxW, uint16_t color) {
  char buf[32];
  strlcpy(buf, text, sizeof buf);
  uint8_t font = s.textWidth(buf, 4) <= maxW ? 4 : 2;
  if (font == 2) fitText(s, buf, maxW, 2);
  s.setTextDatum(TC_DATUM);
  s.setTextColor(color, C_PLANE);
  s.drawString(buf, cx, font == 4 ? y : y + (FONT4_H - FONT2_H) / 2, font);
}

// นับถอยหลังแบบละเอียดถึงวินาที เลือกรูปแบบที่ยาวที่สุดที่ใส่ช่องได้
void drawCountdown(TFT_eSprite& s, uint32_t reset, uint32_t now, int cx, int y, int maxW) {
  if (reset == 0 || now == 0) return;
  char unit[24], compact[24];
  if (reset <= now) {
    strlcpy(unit, "reset now", sizeof unit);
    strlcpy(compact, "now", sizeof compact);
  } else {
    unsigned long t = reset - now;
    unsigned long d = t / 86400, h = (t % 86400) / 3600, m = (t % 3600) / 60, sec = t % 60;
    if (d > 0) {
      snprintf(unit, sizeof unit, "%lud %02luh %02lum", d, h, m);
      snprintf(compact, sizeof compact, "%lud %02lu:%02lu", d, h, m);
    } else if (h > 0) {
      snprintf(unit, sizeof unit, "%luh %02lum %02lus", h, m, sec);
      snprintf(compact, sizeof compact, "%lu:%02lu:%02lu", h, m, sec);
    } else {
      snprintf(unit, sizeof unit, "%lum %02lus", m, sec);
      snprintf(compact, sizeof compact, "%lu:%02lu", m, sec);
    }
  }
  const char* text = unit;
  uint8_t font = 4;
  if (s.textWidth(unit, 4) > maxW) {
    text = compact;
    if (s.textWidth(compact, 4) > maxW) {
      text = s.textWidth(unit, 2) <= maxW ? unit : compact;
      font = 2;
    }
  }
  s.setTextDatum(TC_DATUM);
  s.setTextColor(C_INK2, C_PLANE);
  s.drawString(text, cx, font == 4 ? y : y + (FONT4_H - FONT2_H) / 2, font);
}

void drawHeader(const UiState& st) {
  if (!hdr.created()) {
    hdr.setColorDepth(16);
    hdr.createSprite(W, HEADER_H);
  }
  hdr.fillSprite(C_SURFACE);
  hdr.setTextDatum(ML_DATUM);
  hdr.setTextColor(C_INK, C_SURFACE);
  const char* title = st.snap ? CAT_TITLE[pageCat[page]] : "AIMonitor";
  hdr.drawString(title, 6, HEADER_H / 2 + 1, 4);
  const int titleRight = 6 + hdr.textWidth(title, 4) + 4;

  char buf[32];
  hdr.setTextDatum(MR_DATUM);
  if (st.snap) {
    uint32_t local = st.epochNow + st.snap->tzMin * 60;
    snprintf(buf, sizeof buf, "%02lu:%02lu", (unsigned long)((local / 3600) % 24),
             (unsigned long)((local / 60) % 60));
    hdr.setTextColor(C_INK2, C_SURFACE);
    hdr.drawString(buf, W - 6, HEADER_H / 2, 2);
  }

  // ปุ่มหมุนจอ
  hdr.drawRoundRect(btnX(), BTN_Y, BTN_W, BTN_H, 4, C_TRACK);
  drawRotateIcon(hdr, btnX() + BTN_W / 2, HEADER_H / 2, C_INK2, C_SURFACE);

  // ข้อความสถานะ/จุดบอกหน้า อยู่กึ่งกลางระหว่างชื่อหน้ากับปุ่ม
  const int mid = (titleRight + btnX()) / 2;
  const int room = btnX() - titleRight - 4;
  hdr.setTextDatum(MC_DATUM);
  if (!st.snap) {
    hdr.setTextColor(C_MUTED, C_SURFACE);
    if (hdr.textWidth("waiting", 2) <= room) hdr.drawString("waiting", mid, HEADER_H / 2, 2);
  } else if (st.linkStale) {
    snprintf(buf, sizeof buf, "no update %lum", (unsigned long)(st.secsSinceFrame / 60));
    hdr.setTextColor(C_SEV[SEV_VERY_HIGH], C_SURFACE);
    hdr.drawString(buf, mid, HEADER_H / 2, hdr.textWidth(buf, 2) <= room ? 2 : 1);
  } else if (pageCount > 1) {
    int dx = min(12, room / pageCount), x = mid - (pageCount - 1) * dx / 2;
    for (int i = 0; i < pageCount; i++, x += dx)
      hdr.fillSmoothCircle(x, HEADER_H / 2, i == page ? 4 : 3, i == page ? C_INK : C_TRACK, C_SURFACE);
  }
  hdr.pushSprite(0, 0);
}

void drawMessage(const char* msg, uint16_t color) {
  tft.fillRect(0, HEADER_H, W, H - HEADER_H, C_PLANE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, C_PLANE);
  tft.drawString(msg, W / 2, BODY_Y + BODY_H / 2, 2);
}

void drawGauge(const Provider& p, const QuotaWindow* w, Category cat, const Layout& L, bool hasSub, int x, int y,
               uint32_t now) {
  spr.fillSprite(C_PLANE);

  const int r = L.ring / 2;
  const int cx = L.cellW / 2, cy = r + 1;
  const int thick = max(6, (int)(r * 0.18f));
  Severity sev = w ? effectiveSeverity(*w) : SEV_NORMAL;
  uint16_t fill = p.stale ? C_MUTED : C_SEV[sev];

  // วงโค้งพื้น + วงโค้งค่า
  spr.drawSmoothArc(cx, cy, r, r - thick, ARC_START, ARC_START + ARC_SWEEP, C_TRACK, C_PLANE, true);
  if (w && w->hasPct && w->pct > 0) {
    uint32_t sweep = (uint32_t)(ARC_SWEEP * min(w->pct, 100.0f) / 100.0f + 0.5f);
    sweep = max<uint32_t>(sweep, 2);
    spr.drawSmoothArc(cx, cy, r, r - thick, ARC_START, ARC_START + sweep, fill, C_PLANE, true);
  }

  // ตัวเลขกลางวง — ไม่มีค่าแสดง "--" ไม่เดา
  char buf[24];
  spr.setTextDatum(MC_DATUM);
  if (w && w->hasPct) {
    int v = (int)(w->pct + 0.5f);
    if (L.ring >= 120) {
      // font 6 มีเฉพาะตัวเลข: วาดเลขแล้วต่อด้วย % ฟอนต์ 4
      snprintf(buf, sizeof buf, "%d", v);
      int nw = spr.textWidth(buf, 6), pw = spr.textWidth("%", 4);
      int left = cx - (nw + pw) / 2;
      spr.setTextColor(C_INK, C_PLANE);
      spr.setTextDatum(ML_DATUM);
      spr.drawString(buf, left, cy, 6);
      spr.drawString("%", left + nw + 1, cy + 6, 4);
      spr.setTextDatum(MC_DATUM);
    } else {
      snprintf(buf, sizeof buf, "%d%%", v);
      spr.setTextColor(C_INK, C_PLANE);
      spr.drawString(buf, cx, cy, L.ring >= 70 ? 4 : 2);
    }
  } else {
    spr.setTextColor(C_MUTED, C_PLANE);
    spr.drawString("--", cx, cy, L.ring >= 70 ? 4 : 2);
  }

  // สถานะใต้ตัวเลข (ช่องว่างด้านล่างของวงโค้ง)
  char status[sizeof p.err];
  uint16_t statusColor;
  if (!w) {
    strlcpy(status, p.err[0] ? p.err : "No data", sizeof status);
    statusColor = p.err[0] ? C_SEV[SEV_CRITICAL] : C_MUTED;
  } else if (w->lock[0]) {
    strlcpy(status, "Locked", sizeof status);
    statusColor = C_SEV[SEV_CRITICAL];
  } else if (p.stale) {
    strlcpy(status, "Stale", sizeof status);
    statusColor = C_MUTED;
  } else {
    strlcpy(status, w->hasPct ? SEV_LABEL[sev] : "", sizeof status);
    statusColor = C_SEV[sev];
  }
  if (status[0]) {
    uint8_t font = L.ring >= 70 ? 2 : 1;
    fitText(spr, status, (int)(r * 1.3f), font);
    spr.setTextColor(statusColor, C_PLANE);
    int sy = L.ring >= 120 ? cy + r * 0.62f : cy + r * 0.72f;
    spr.drawString(status, cx, sy, font);
  }

  // ชื่อ service / ชื่อย่อยของหน้าต่าง / นับถอยหลัง
  const int maxW = L.cellW - 4;
  int ty = L.ring + 2;
  drawFitted(spr, shortName(p), cx, ty, maxW, p.stale ? C_MUTED : C_INK);
  ty += FONT4_H;
  if (hasSub) {
    char sub[sizeof w->title] = "";
    if (w) subLabel(*w, cat, sub, sizeof sub);
    if (sub[0]) {
      fitText(spr, sub, maxW, 2);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(C_MUTED, C_PLANE);
      spr.drawString(sub, cx, ty, 2);
    }
    ty += FONT2_H;
  }
  if (w) drawCountdown(spr, w->reset, now, cx, ty, maxW);

  spr.pushSprite(x, y);
}

void drawWaiting() {
  tft.fillRect(0, HEADER_H, W, H - HEADER_H, C_PLANE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_INK2, C_PLANE);
  const int cy = HEADER_H + (H - HEADER_H) / 2;
  if (tft.textWidth("Waiting for AIMonitor", 4) <= W - 12) {
    tft.drawString("Waiting for AIMonitor", W / 2, cy - 14, 4);
  } else {
    tft.drawString("Waiting for", W / 2, cy - 40, 4);
    tft.drawString("AIMonitor", W / 2, cy - 12, 4);
  }
  tft.setTextColor(C_MUTED, C_PLANE);
  tft.drawString("USB serial 115200", W / 2, cy + 20, 2);
}

}  // namespace

void uiBegin() { uiOrientationChanged(); }

void uiOrientationChanged() {
  W = tft.width();
  H = tft.height();
  BODY_H = H - BODY_Y;
  hdr.deleteSprite();  // ความกว้างแถบบนเปลี่ยน สร้างใหม่ในรอบวาดถัดไป
  lastDrawnKey = -1;   // บังคับวาดใหม่ทั้งหน้า
  tft.fillScreen(C_PLANE);
}

bool uiHitRotateButton(int x, int y) {
  return x >= btnX() - BTN_HIT_SLACK && x <= btnX() + BTN_W + BTN_HIT_SLACK && y <= HEADER_H + BTN_HIT_SLACK;
}

void uiNextPage() {
  if (pageCount > 1) page = (page + 1) % pageCount;
}

int uiPageCount() { return pageCount; }

void uiRender(const UiState& st) {
  buildPages(st.snap);
  drawHeader(st);

  if (!st.snap) {
    if (lastDrawnKey != -2) drawWaiting();
    lastDrawnKey = -2;
    return;
  }
  if (st.snap->count == 0) {
    if (lastDrawnKey != -3) drawMessage("No services reported", C_MUTED);
    lastDrawnKey = -3;
    return;
  }

  const Category cat = pageCat[page];
  Item items[MAX_ITEMS];
  const int n = collectItems(*st.snap, cat, items);
  bool hasSub = false;
  char sub[24];
  for (int i = 0; i < n && !hasSub; i++) {
    if (items[i].w < 0) continue;
    subLabel(st.snap->p[items[i].p].w[items[i].w], cat, sub, sizeof sub);
    hasSub = sub[0] != 0;
  }

  const Layout L = computeLayout(n, hasSub);
  const int key = (page * 32 + n) * 2 + (hasSub ? 1 : 0);
  if (key != lastDrawnKey) tft.fillRect(0, HEADER_H, W, H - HEADER_H, C_PLANE);
  lastDrawnKey = key;

  ensureSprite(L.cellW, L.cellH);
  if (!sprW) {
    drawMessage("Out of memory", C_SEV[SEV_CRITICAL]);
    lastDrawnKey = -4;
    return;
  }

  const int y0 = BODY_Y + (BODY_H - L.rows * L.cellH) / 2;
  for (int i = 0; i < n; i++) {
    int row = i / L.cols, col = i % L.cols;
    // แถวสุดท้ายที่ไม่เต็ม: จัดกึ่งกลาง
    int inRow = (row == L.rows - 1) ? n - row * L.cols : L.cols;
    int rowX0 = (W - inRow * L.cellW) / 2;
    const Provider& p = st.snap->p[items[i].p];
    const QuotaWindow* w = items[i].w >= 0 ? &p.w[items[i].w] : nullptr;
    drawGauge(p, w, cat, L, hasSub, rowX0 + col * L.cellW, y0 + row * L.cellH, st.epochNow);
  }
}
