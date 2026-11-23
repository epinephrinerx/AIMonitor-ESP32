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
const uint16_t C_ACCENT = rgb(0x3987e5);
const uint16_t C_SEV[4] = {rgb(0x0ca30c), rgb(0xfab219), rgb(0xec835a), rgb(0xd03b3b)};
const char* const SEV_LABEL[4] = {"Normal", "High", "Very high", "Critical"};

// ขนาดจอเปลี่ยนตามทิศ: แนวนอน 320x240, แนวตั้ง 240x320 (อัปเดตใน uiOrientationChanged)
int W = 320, H = 240;
constexpr int HEADER_H = 24;
constexpr int PROV_H = 22;
constexpr int BODY_Y = HEADER_H + PROV_H;
int BODY_H = 240 - BODY_Y;
constexpr int CAPTION_H = 36;  // ชื่อหน้าต่าง + นับถอยหลัง ใต้วงโค้ง

// ปุ่มหมุนจอ: อยู่ซ้ายของนาฬิกาในแถบบน
constexpr int CLOCK_W = 36;  // "00:00" ฟอนต์ 2
constexpr int BTN_W = 28, BTN_H = 20;
constexpr int BTN_HIT_SLACK = 10;  // ขยายพื้นที่กดเผื่อ touch คลาดเล็กน้อย
int btnX() { return W - 6 - CLOCK_W - 6 - BTN_W; }
constexpr int BTN_Y = (HEADER_H - BTN_H) / 2;
constexpr int TITLE_RIGHT = 78;  // ขอบขวาของ "AI Monitor"

// รูปทรงเดียวกับ GaugeControl.cs: เริ่ม 140° กวาด 260° (WPF, 0° = 3 นาฬิกา ตามเข็ม)
// TFT_eSPI นับ 0° = 6 นาฬิกา ตามเข็ม → 50° ถึง 310°
constexpr uint32_t ARC_START = 50;
constexpr uint32_t ARC_SWEEP = 260;

struct Layout {
  int n, cols, rows, cellW, cellH, ring, x0, y0;
};

TFT_eSprite spr(&tft);
TFT_eSprite hdr(&tft);
int sprW = 0, sprH = 0;
int pageCount = 1;
int page = 0;
int lastDrawnPage = -1;
int lastDrawnLayoutN = -1;

// เลือกจำนวนคอลัมน์ที่ให้ gauge ใหญ่ที่สุดในทิศจอปัจจุบัน
Layout computeLayout(int n) {
  Layout best{};
  for (int cols = 1; cols <= min(n, 4); cols++) {
    Layout L{};
    L.n = n;
    L.cols = cols;
    L.rows = (n + cols - 1) / cols;
    L.cellW = min(160, W / cols);
    L.ring = min(L.cellW - 10, BODY_H / L.rows - CAPTION_H - 2);
    if (L.ring <= best.ring) continue;
    L.cellH = L.ring + CAPTION_H;
    L.x0 = (W - L.cols * L.cellW) / 2;
    L.y0 = BODY_Y + (BODY_H - L.rows * L.cellH) / 2;
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

void formatCountdown(char* buf, size_t n, uint32_t reset, uint32_t now) {
  if (reset == 0 || now == 0) {
    buf[0] = 0;
    return;
  }
  if (reset <= now) {
    strlcpy(buf, "reset now", n);
    return;
  }
  uint32_t s = reset - now;
  uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  if (d > 0) snprintf(buf, n, "%lud %luh", (unsigned long)d, (unsigned long)h);
  else if (h > 0) snprintf(buf, n, "%luh %02lum", (unsigned long)h, (unsigned long)m);
  else snprintf(buf, n, "%lum", (unsigned long)max<uint32_t>(m, 1));
}

void fitText(TFT_eSprite& s, char* text, int maxW, uint8_t font) {
  while (strlen(text) > 1 && s.textWidth(text, font) > maxW) text[strlen(text) - 1] = 0;
}

void drawHeader(const UiState& st) {
  if (!hdr.created()) {
    hdr.setColorDepth(16);
    hdr.createSprite(W, HEADER_H);
  }
  hdr.fillSprite(C_SURFACE);
  hdr.setTextDatum(ML_DATUM);
  hdr.setTextColor(C_INK, C_SURFACE);
  hdr.drawString("AI Monitor", 6, HEADER_H / 2, 2);

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

  // ข้อความสถานะ/จุดบอกหน้า อยู่กึ่งกลางระหว่างชื่อแอปกับปุ่ม
  const int mid = (TITLE_RIGHT + btnX()) / 2;
  const int room = btnX() - TITLE_RIGHT - 4;
  hdr.setTextDatum(MC_DATUM);
  if (!st.snap) {
    hdr.setTextColor(C_MUTED, C_SURFACE);
    hdr.drawString("waiting", mid, HEADER_H / 2, 2);
  } else if (st.linkStale) {
    snprintf(buf, sizeof buf, "no update %lum", (unsigned long)(st.secsSinceFrame / 60));
    hdr.setTextColor(C_SEV[SEV_VERY_HIGH], C_SURFACE);
    hdr.drawString(buf, mid, HEADER_H / 2, hdr.textWidth(buf, 2) <= room ? 2 : 1);
  } else if (pageCount > 1) {
    // จุดบอกหน้า
    int dx = min(12, room / pageCount), x = mid - (pageCount - 1) * dx / 2;
    for (int i = 0; i < pageCount; i++, x += dx)
      hdr.fillSmoothCircle(x, HEADER_H / 2, i == page ? 4 : 3, i == page ? C_INK : C_TRACK, C_SURFACE);
  }
  hdr.pushSprite(0, 0);
}

void drawProviderTitle(const Provider& p, bool force) {
  static bool lastStale = false;
  if (!force && p.stale == lastStale) return;  // วาดเฉพาะเมื่อเปลี่ยน กันกระพริบ
  lastStale = p.stale;
  tft.fillRect(0, HEADER_H, W, PROV_H, C_PLANE);
  tft.setTextDatum(BL_DATUM);
  tft.setTextColor(p.stale ? C_MUTED : C_ACCENT, C_PLANE);
  tft.drawString(p.name, 8, HEADER_H + PROV_H - 2, 2);
  if (p.stale) {
    tft.setTextDatum(BR_DATUM);
    tft.setTextColor(C_SEV[SEV_HIGH], C_PLANE);
    tft.drawString("stale", W - 8, HEADER_H + PROV_H - 2, 2);
  }
  tft.drawFastHLine(8, HEADER_H + PROV_H - 1, W - 16, C_TRACK);
}

void drawMessage(const char* msg, uint16_t color) {
  tft.fillRect(0, BODY_Y, W, BODY_H, C_PLANE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, C_PLANE);
  tft.drawString(msg, W / 2, BODY_Y + BODY_H / 2, 2);
}

void drawGauge(const Provider& p, const QuotaWindow& w, const Layout& L, int x, int y, uint32_t now) {
  spr.fillSprite(C_PLANE);

  const int r = L.ring / 2;
  const int cx = L.cellW / 2, cy = r + 1;
  const int thick = max(6, (int)(r * 0.18f));
  Severity sev = effectiveSeverity(w);
  uint16_t fill = p.stale ? C_MUTED : C_SEV[sev];

  // วงโค้งพื้น + วงโค้งค่า
  spr.drawSmoothArc(cx, cy, r, r - thick, ARC_START, ARC_START + ARC_SWEEP, C_TRACK, C_PLANE, true);
  if (w.hasPct && w.pct > 0) {
    uint32_t sweep = (uint32_t)(ARC_SWEEP * min(w.pct, 100.0f) / 100.0f + 0.5f);
    sweep = max<uint32_t>(sweep, 2);
    spr.drawSmoothArc(cx, cy, r, r - thick, ARC_START, ARC_START + sweep, fill, C_PLANE, true);
  }

  // ตัวเลขกลางวง — ไม่มีค่าแสดง "--" ไม่เดา
  char buf[24];
  spr.setTextDatum(MC_DATUM);
  if (w.hasPct) {
    int v = (int)(w.pct + 0.5f);
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
      spr.drawString(buf, cx, cy, L.ring >= 80 ? 4 : 2);
    }
  } else {
    spr.setTextColor(C_MUTED, C_PLANE);
    spr.drawString("--", cx, cy, L.ring >= 80 ? 4 : 2);
  }

  // สถานะใต้ตัวเลข (ช่องว่างด้านล่างของวงโค้ง)
  const char* status = w.lock[0] ? "Locked" : (p.stale ? "Stale" : (w.hasPct ? SEV_LABEL[sev] : ""));
  uint16_t statusColor = w.lock[0] ? C_SEV[SEV_CRITICAL] : (p.stale ? C_MUTED : C_SEV[sev]);
  if (status[0]) {
    spr.setTextColor(statusColor, C_PLANE);
    int sy = L.ring >= 120 ? cy + r * 0.62f : cy + r * 0.72f;
    spr.drawString(status, cx, sy, L.ring >= 80 ? 2 : 1);
  }

  // ชื่อหน้าต่าง + นับถอยหลัง
  int ty = L.ring + 2;
  char title[sizeof w.title];
  strlcpy(title, w.title, sizeof title);
  fitText(spr, title, L.cellW - 4, 2);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(C_INK, C_PLANE);
  spr.drawString(title, cx, ty, 2);

  formatCountdown(buf, sizeof buf, w.reset, now);
  if (buf[0]) {
    spr.setTextColor(C_INK2, C_PLANE);
    spr.drawString(buf, cx, ty + 17, 2);
  }

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
  lastDrawnPage = -1;  // บังคับวาดใหม่ทั้งหน้า
  lastDrawnLayoutN = -1;
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
  pageCount = st.snap && st.snap->count > 0 ? st.snap->count : 1;
  if (page >= pageCount) page = 0;
  drawHeader(st);

  if (!st.snap) {
    if (lastDrawnPage != -2) drawWaiting();
    lastDrawnPage = -2;
    return;
  }
  if (st.snap->count == 0) {
    drawMessage("No services reported", C_MUTED);
    lastDrawnPage = -3;
    return;
  }

  const Provider& p = st.snap->p[page];
  const bool pageChanged = page != lastDrawnPage;
  drawProviderTitle(p, pageChanged);

  if (p.err[0] || p.count == 0) {
    drawMessage(p.err[0] ? p.err : "No quota reported", p.err[0] ? C_SEV[SEV_CRITICAL] : C_MUTED);
    lastDrawnPage = page;
    lastDrawnLayoutN = -1;
    return;
  }

  Layout L = computeLayout(p.count);
  if (pageChanged || L.n != lastDrawnLayoutN) tft.fillRect(0, BODY_Y, W, BODY_H, C_PLANE);
  ensureSprite(L.cellW, L.cellH);
  if (!sprW) {
    drawMessage("Out of memory", C_SEV[SEV_CRITICAL]);
    return;
  }

  for (int i = 0; i < p.count; i++) {
    int row = i / L.cols, col = i % L.cols;
    // แถวสุดท้ายที่ไม่เต็ม: จัดกึ่งกลาง
    int inRow = (row == L.rows - 1) ? p.count - row * L.cols : L.cols;
    int rowX0 = (W - inRow * L.cellW) / 2;
    drawGauge(p, p.w[i], L, rowX0 + col * L.cellW, L.y0 + row * L.cellH, st.epochNow);
  }
  lastDrawnPage = page;
  lastDrawnLayoutN = L.n;
}
