#include "ui.h"
#include <TFT_eSPI.h>
#include "logos.h"

extern TFT_eSPI tft;

namespace {

// แปลงสี 0xRRGGBB เป็น RGB565
constexpr uint16_t rgb(uint32_t hex) {
  return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
}
const uint16_t C_ACCENT = rgb(0x3987e5);
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
// + แถบเวลาที่เหลือก่อนรีเซ็ต
constexpr int FONT4_H = 26, FONT2_H = 16;
constexpr int BAR_H = 6, BAR_GAP = 3;
int captionHeight(bool hasSub) { return FONT4_H + (hasSub ? FONT2_H : 0) + FONT4_H + BAR_GAP + BAR_H + 4; }

// ความยาวหน้าต่างแต่ละหมวด ใช้คำนวณแถบเวลา (PC ไม่ได้ส่งเวลาเริ่มมา); 0 = ไม่ทราบ ไม่วาดแถบ
constexpr uint32_t CAT_SECONDS[] = {5 * 3600, 7 * 86400, 0};

// ธีมสว่าง/มืด: พื้นทั้งจอเป็นสีเดียว กรอบของแต่ละบริการและแถบบนเป็นสีตรงข้ามกับพื้น
struct Theme {
  uint16_t bg, ink, ink2, muted, track;  // พื้นจอและของในช่อง
  uint16_t frame;                         // กรอบบริการ = พื้นแถบบน
  uint16_t hdrInk, hdrInk2, hdrTrack;     // ตัวอักษรและเส้นบนแถบบน
};
const Theme THEME_DARK = {rgb(0x0d0d0d), rgb(0xffffff), rgb(0xc3c2b7), rgb(0x898781), rgb(0x3a3a37),
                          rgb(0xffffff), rgb(0x0d0d0d), rgb(0x3a3a37), rgb(0xb4b3ad)};
const Theme THEME_LIGHT = {rgb(0xffffff), rgb(0x0d0d0d), rgb(0x3a3a37), rgb(0x6b6a65), rgb(0xd6d5cf),
                           rgb(0x0d0d0d), rgb(0xffffff), rgb(0xc3c2b7), rgb(0x5a5955)};
Theme cc = THEME_DARK;
bool lightTheme = false;

// กรอบรอบแต่ละบริการ: เว้นขอบนอก FRAME_MARGIN, เส้นหนา FRAME_W, เว้นด้านในอีก FRAME_PAD
constexpr int FRAME_MARGIN = 2, FRAME_W = 2, FRAME_PAD = 3;
constexpr int FRAME_INSET = FRAME_MARGIN + FRAME_W + FRAME_PAD;

// ปุ่มหมุนจอ: อยู่ซ้ายของนาฬิกาในแถบบน; ปุ่มสลับธีมอยู่ซ้ายของปุ่มหมุนจอ
constexpr int CLOCK_W = 36;  // "00:00" ฟอนต์ 2
constexpr int BTN_W = 28, BTN_H = 22, BTN_GAP = 6;
constexpr int BTN_HIT_SLACK = 10;  // ขยายพื้นที่กดเผื่อ touch คลาดเล็กน้อย
int btnX() { return W - 6 - CLOCK_W - 6 - BTN_W; }
int themeBtnX() { return btnX() - BTN_GAP - BTN_W; }
constexpr int BTN_Y = (HEADER_H - BTN_H) / 2;

// รูปทรงเดียวกับ GaugeControl.cs: เริ่ม 140° กวาด 260° (WPF, 0° = 3 นาฬิกา ตามเข็ม)
// TFT_eSPI นับ 0° = 6 นาฬิกา ตามเข็ม → 50° ถึง 310°
constexpr uint32_t ARC_START = 50;
constexpr uint32_t ARC_SWEEP = 260;
constexpr float GAUGE_SCALE = 0.9f;  // ขนาดวงเทียบกับพื้นที่ที่จัดไว้

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
    L.cellW = min(170, W / cols);
    L.captionH = capH;
    L.ring = min(L.cellW - 2 * FRAME_INSET, BODY_H / L.rows - capH - 2 * FRAME_INSET - 2);
    if (L.ring <= best.ring) continue;
    L.cellH = L.ring + capH + 2 * FRAME_INSET;
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

// ไอคอนธีม: วงกลมครึ่งทึบครึ่งโปร่ง
void drawThemeIcon(TFT_eSprite& s, int cx, int cy, uint16_t color, uint16_t bg) {
  const int r = 7;
  s.fillSmoothCircle(cx, cy, r, color, bg);
  s.fillSmoothCircle(cx, cy, r - 2, bg, color);
  s.fillRect(cx, cy - r + 2, r - 1, 2 * r - 3, color);  // ครึ่งขวาทึบ
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

// โลโก้จากไฟล์ของแต่ละบริการ (src/logos.h) ผสม alpha กับพื้นของช่อง; size 20 หรือ 14
void drawLogo(TFT_eSprite& s, const Provider& p, int cx, int cy, int size) {
  const bool big = size >= 20;
  const uint8_t* alpha = nullptr;
  const uint16_t* color = nullptr;  // nullptr = mask ขาวดำ วาดด้วยสีตัวอักษรของช่อง
  if (strcmp(p.id, "claude") == 0) {
    alpha = big ? LOGO_CLAUDE_20_A : LOGO_CLAUDE_14_A;
    color = big ? LOGO_CLAUDE_20_C : LOGO_CLAUDE_14_C;
  } else if (strcmp(p.id, "openai") == 0) {
    alpha = big ? LOGO_CODEX_20_A : LOGO_CODEX_14_A;
  } else if (strcmp(p.id, "agy") == 0) {
    alpha = big ? LOGO_AGY_20_A : LOGO_AGY_14_A;
    color = big ? LOGO_AGY_20_C : LOGO_AGY_14_C;
  }
  const int n = big ? 20 : 14;
  const int x0 = cx - n / 2, y0 = cy - n / 2;
  if (!alpha) {
    // บริการอื่น: วงกลมพร้อมอักษรตัวแรก
    s.fillSmoothCircle(cx, cy, n / 2, C_ACCENT, cc.bg);
    char letter[2] = {shortName(p)[0], 0};
    s.setTextDatum(MC_DATUM);
    s.setTextColor(rgb(0xffffff), C_ACCENT);
    s.drawString(letter, cx, cy + 1, big ? 2 : 1);
    return;
  }
  for (int i = 0; i < n * n; i++) {
    uint8_t a = pgm_read_byte(&alpha[i]);
    if (!a) continue;
    uint16_t fg = color ? pgm_read_word(&color[i]) : cc.ink;
    s.drawPixel(x0 + i % n, y0 + i / n, a == 255 ? fg : s.alphaBlend(a, fg, cc.bg));
  }
}

// ขนาดโลโก้ที่คู่กับฟอนต์ชื่อ
int logoSize(uint8_t font) { return font == 4 ? 20 : 14; }

// ฟอนต์ชื่อของทั้งหน้า: ฟอนต์ 4 ถ้าชื่อทุกบริการพอดีช่อง ไม่อย่างนั้นใช้ฟอนต์ 2 ทุกบริการ ขนาดเท่ากันหมด
uint8_t pageNameFont(const Snapshot& s, const Item* items, int n, int maxW) {
  for (int i = 0; i < n; i++)
    if (logoSize(4) + 4 + spr.textWidth(shortName(s.p[items[i].p]), 4) > maxW) return 2;
  return 4;
}

// โลโก้ + ชื่อ service กึ่งกลางช่อง (ตัดท้ายถ้ายาวเกินแม้ใช้ฟอนต์ 2)
void drawName(TFT_eSprite& s, const Provider& p, uint8_t font, int cx, int y, int maxW) {
  char buf[32];
  strlcpy(buf, shortName(p), sizeof buf);
  const int icon = logoSize(font);
  fitText(s, buf, maxW - icon - 4, font);
  const int total = icon + 4 + s.textWidth(buf, font);
  const int left = cx - total / 2, mid = y + FONT4_H / 2;
  drawLogo(s, p, left + icon / 2, mid, icon);
  s.setTextDatum(ML_DATUM);
  s.setTextColor(p.stale ? cc.muted : cc.ink, cc.bg);
  s.drawString(buf, left + icon + 4, mid + (font == 4 ? 1 : 0), font);
}

// นับถอยหลังแบบละเอียดถึงวินาที เลือกรูปแบบที่ยาวที่สุดที่ใส่ช่องได้
void drawCountdown(TFT_eSprite& s, uint32_t reset, uint32_t now, bool thin, int cx, int y, int maxW) {
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
  s.setTextColor(cc.ink2, cc.bg);
  if (thin) {
    // Session: "xh yym zzs" เสมอ ฟอนต์ 2 ขนาดเดียวกับหน้า Weekly
    char hms[24];
    unsigned long t = reset > now ? reset - now : 0;
    snprintf(hms, sizeof hms, "%luh %02lum %02lus", t / 3600, (t % 3600) / 60, t % 60);
    s.setTextDatum(TC_DATUM);
    s.drawString(hms, cx, y + (FONT4_H - FONT2_H) / 2, 2);
  } else {
    // Weekly: แบบเต็มด้วยฟอนต์ 4 ถ้าพอดี ไม่อย่างนั้นฟอนต์ 2
    s.setTextDatum(TC_DATUM);
    const char* text = s.textWidth(unit, 4) <= maxW ? unit : (s.textWidth(compact, 4) <= maxW ? compact : nullptr);
    if (text) {
      s.drawString(text, cx, y, 4);
    } else {
      text = s.textWidth(unit, 2) <= maxW ? unit : compact;
      s.drawString(text, cx, y + (FONT4_H - FONT2_H) / 2, 2);
    }
  }
  s.setTextFont(1);  // กลับไปใช้ฟอนต์ในตัว
}

void drawHeader(const UiState& st) {
  if (!hdr.created()) {
    hdr.setColorDepth(16);
    hdr.createSprite(W, HEADER_H);
  }
  hdr.fillSprite(cc.frame);
  hdr.setTextDatum(ML_DATUM);
  hdr.setTextColor(cc.hdrInk, cc.frame);
  const char* title = st.snap ? CAT_TITLE[pageCat[page]] : "AIMonitor";
  hdr.drawString(title, 6, HEADER_H / 2 + 1, 4);
  const int titleRight = 6 + hdr.textWidth(title, 4) + 4;

  char buf[32];
  hdr.setTextDatum(MR_DATUM);
  if (st.snap) {
    uint32_t local = st.epochNow + st.snap->tzMin * 60;
    snprintf(buf, sizeof buf, "%02lu:%02lu", (unsigned long)((local / 3600) % 24),
             (unsigned long)((local / 60) % 60));
    hdr.setTextColor(cc.hdrInk2, cc.frame);
    hdr.drawString(buf, W - 6, HEADER_H / 2, 2);
  }

  // ปุ่มสลับธีม + ปุ่มหมุนจอ
  hdr.drawRoundRect(themeBtnX(), BTN_Y, BTN_W, BTN_H, 4, cc.hdrTrack);
  drawThemeIcon(hdr, themeBtnX() + BTN_W / 2, HEADER_H / 2, cc.hdrInk2, cc.frame);
  hdr.drawRoundRect(btnX(), BTN_Y, BTN_W, BTN_H, 4, cc.hdrTrack);
  drawRotateIcon(hdr, btnX() + BTN_W / 2, HEADER_H / 2, cc.hdrInk2, cc.frame);

  // ข้อความสถานะ/จุดบอกหน้า อยู่กึ่งกลางระหว่างชื่อหน้ากับปุ่ม
  const int mid = (titleRight + themeBtnX()) / 2;
  const int room = themeBtnX() - titleRight - 4;
  hdr.setTextDatum(MC_DATUM);
  if (!st.snap) {
    hdr.setTextColor(cc.hdrInk2, cc.frame);
    if (hdr.textWidth("waiting", 2) <= room) hdr.drawString("waiting", mid, HEADER_H / 2, 2);
  } else if (st.linkStale) {
    snprintf(buf, sizeof buf, "no update %lum", (unsigned long)(st.secsSinceFrame / 60));
    hdr.setTextColor(C_SEV[SEV_VERY_HIGH], cc.frame);
    const uint8_t font = hdr.textWidth(buf, 2) <= room ? 2 : 1;
    fitText(hdr, buf, room, font);
    hdr.drawString(buf, mid, HEADER_H / 2, font);
  } else if (pageCount > 1) {
    int dx = min(12, room / pageCount), x = mid - (pageCount - 1) * dx / 2;
    for (int i = 0; i < pageCount; i++, x += dx)
      hdr.fillSmoothCircle(x, HEADER_H / 2, i == page ? 4 : 3, i == page ? cc.hdrInk : cc.hdrTrack, cc.frame);
  }
  hdr.pushSprite(0, 0);
}

void drawMessage(const char* msg, uint16_t color) {
  tft.fillRect(0, HEADER_H, W, H - HEADER_H, cc.bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, cc.bg);
  tft.drawString(msg, W / 2, BODY_Y + BODY_H / 2, 2);
}

void drawGauge(const Provider& p, const QuotaWindow* w, Category cat, const Layout& L, bool hasSub, uint8_t nameFont,
               int x, int y, uint32_t now) {
  spr.fillSprite(cc.bg);

  // กรอบของบริการ สีตรงข้ามกับพื้น (สีเดียวกับแถบบน)
  for (int k = 0; k < FRAME_W; k++)
    spr.drawRoundRect(FRAME_MARGIN + k, FRAME_MARGIN + k, L.cellW - 2 * (FRAME_MARGIN + k),
                      L.cellH - 2 * (FRAME_MARGIN + k), 8 - k, cc.frame);

  // วงโค้งเล็กกว่าพื้นที่ที่จัดไว้ (GAUGE_SCALE) จุดกึ่งกลางคงเดิม
  const int r = (int)(L.ring * GAUGE_SCALE) / 2;
  const int d = 2 * r;
  const int cx = L.cellW / 2, cy = FRAME_INSET + L.ring / 2 + 1;
  const int thick = max(6, (int)(r * 0.18f));
  Severity sev = w ? effectiveSeverity(*w) : SEV_NORMAL;
  uint16_t fill = p.stale ? cc.muted : C_SEV[sev];

  // วงโค้งพื้น + วงโค้งค่า
  spr.drawSmoothArc(cx, cy, r, r - thick, ARC_START, ARC_START + ARC_SWEEP, cc.track, cc.bg, true);
  if (w && w->hasPct && w->pct > 0) {
    uint32_t sweep = (uint32_t)(ARC_SWEEP * min(w->pct, 100.0f) / 100.0f + 0.5f);
    sweep = max<uint32_t>(sweep, 2);
    spr.drawSmoothArc(cx, cy, r, r - thick, ARC_START, ARC_START + sweep, fill, cc.bg, true);
  }

  // ตัวเลขกลางวง — ไม่มีค่าแสดง "--" ไม่เดา
  // ใช้ FreeSansBold (ตัวเลขสูง 17/34 px) เล็กกว่าฟอนต์ 4/6 ในตัว (18/36 px) เล็กน้อย
  char buf[24];
  spr.setTextDatum(MC_DATUM);
  if (d >= 70) {
    spr.setFreeFont(d >= 120 ? &FreeSansBold24pt7b : &FreeSansBold12pt7b);
    if (w && w->hasPct) snprintf(buf, sizeof buf, "%d%%", (int)(w->pct + 0.5f));
    else strlcpy(buf, "--", sizeof buf);
    spr.setTextColor(w && w->hasPct ? cc.ink : cc.muted, cc.bg);
    spr.drawString(buf, cx, cy);
    spr.setTextFont(1);  // กลับไปใช้ฟอนต์ในตัว
  } else {
    if (w && w->hasPct) snprintf(buf, sizeof buf, "%d%%", (int)(w->pct + 0.5f));
    else strlcpy(buf, "--", sizeof buf);
    spr.setTextColor(w && w->hasPct ? cc.ink : cc.muted, cc.bg);
    spr.drawString(buf, cx, cy, 2);
  }

  // สถานะใต้ตัวเลข (ช่องว่างด้านล่างของวงโค้ง)
  char status[sizeof p.err];
  uint16_t statusColor;
  if (!w) {
    strlcpy(status, p.err[0] ? p.err : "No data", sizeof status);
    statusColor = p.err[0] ? C_SEV[SEV_CRITICAL] : cc.muted;
  } else if (w->lock[0]) {
    strlcpy(status, "Locked", sizeof status);
    statusColor = C_SEV[SEV_CRITICAL];
  } else if (p.stale) {
    strlcpy(status, "Stale", sizeof status);
    statusColor = cc.muted;
  } else {
    strlcpy(status, w->hasPct ? SEV_LABEL[sev] : "", sizeof status);
    statusColor = C_SEV[sev];
  }
  if (status[0]) {
    uint8_t font = d >= 70 ? 2 : 1;
    fitText(spr, status, (int)(r * 1.3f), font);
    spr.setTextColor(statusColor, cc.bg);
    int sy = d >= 120 ? cy + r * 0.62f : cy + r * 0.72f;
    spr.drawString(status, cx, sy, font);
  }

  // โลโก้ + ชื่อ service / ชื่อย่อยของหน้าต่าง / นับถอยหลัง / แถบเวลาที่เหลือ
  const int maxW = L.cellW - 2 * FRAME_INSET;
  int ty = FRAME_INSET + L.ring + 2;
  drawName(spr, p, nameFont, cx, ty, maxW);
  ty += FONT4_H;
  if (hasSub) {
    char sub[sizeof w->title] = "";
    if (w) subLabel(*w, cat, sub, sizeof sub);
    if (sub[0]) {
      fitText(spr, sub, maxW, 2);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(cc.muted, cc.bg);
      spr.drawString(sub, cx, ty, 2);
    }
    ty += FONT2_H;
  }
  if (w) drawCountdown(spr, w->reset, now, cat == CAT_SESSION, cx, ty, maxW);
  ty += FONT4_H + BAR_GAP;

  // แถบนับถอยหลัง: เต็มเมื่อหน้าต่างเพิ่งเริ่ม หดลงจนหมดตอนรีเซ็ต
  const uint32_t span = CAT_SECONDS[cat];
  if (w && span && w->reset && now) {
    const int bx = FRAME_INSET + 2, bw = L.cellW - 2 * bx;
    float left = w->reset > now ? (float)(w->reset - now) / span : 0.0f;
    left = constrain(left, 0.0f, 1.0f);
    spr.fillSmoothRoundRect(bx, ty, bw, BAR_H, BAR_H / 2, cc.track, cc.bg);
    int fw = (int)(bw * left + 0.5f);
    if (fw >= BAR_H) spr.fillSmoothRoundRect(bx, ty, fw, BAR_H, BAR_H / 2, p.stale ? cc.muted : C_ACCENT, cc.track);
  }

  spr.pushSprite(x, y);
}

void drawWaiting() {
  tft.fillRect(0, HEADER_H, W, H - HEADER_H, cc.bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cc.ink2, cc.bg);
  const int cy = HEADER_H + (H - HEADER_H) / 2;
  if (tft.textWidth("Waiting for AIMonitor", 4) <= W - 12) {
    tft.drawString("Waiting for AIMonitor", W / 2, cy - 14, 4);
  } else {
    tft.drawString("Waiting for", W / 2, cy - 40, 4);
    tft.drawString("AIMonitor", W / 2, cy - 12, 4);
  }
  tft.setTextColor(cc.muted, cc.bg);
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
  tft.fillScreen(cc.bg);
}

// สองปุ่มติดกัน: แบ่งพื้นที่เผื่อพลาดที่กึ่งกลางช่องว่างระหว่างปุ่ม
bool uiHitRotateButton(int x, int y) {
  return x >= btnX() - BTN_GAP / 2 && x <= btnX() + BTN_W + BTN_HIT_SLACK && y <= HEADER_H + BTN_HIT_SLACK;
}

bool uiHitThemeButton(int x, int y) {
  return x >= themeBtnX() - BTN_HIT_SLACK && x < btnX() - BTN_GAP / 2 && y <= HEADER_H + BTN_HIT_SLACK;
}

void uiSetLightTheme(bool light) {
  lightTheme = light;
  cc = light ? THEME_LIGHT : THEME_DARK;
  lastDrawnKey = -1;  // วาดใหม่ทั้งจอด้วยสีใหม่
}

bool uiLightTheme() { return lightTheme; }

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
    if (lastDrawnKey != -3) drawMessage("No services reported", cc.muted);
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
  if (key != lastDrawnKey) tft.fillRect(0, HEADER_H, W, H - HEADER_H, cc.bg);
  lastDrawnKey = key;

  ensureSprite(L.cellW, L.cellH);
  if (!sprW) {
    drawMessage("Out of memory", C_SEV[SEV_CRITICAL]);
    lastDrawnKey = -4;
    return;
  }

  const uint8_t nameFont = pageNameFont(*st.snap, items, n, L.cellW - 2 * FRAME_INSET);
  const int y0 = BODY_Y + (BODY_H - L.rows * L.cellH) / 2;
  for (int i = 0; i < n; i++) {
    int row = i / L.cols, col = i % L.cols;
    // แถวสุดท้ายที่ไม่เต็ม: จัดกึ่งกลาง
    int inRow = (row == L.rows - 1) ? n - row * L.cols : L.cols;
    int rowX0 = (W - inRow * L.cellW) / 2;
    const Provider& p = st.snap->p[items[i].p];
    const QuotaWindow* w = items[i].w >= 0 ? &p.w[items[i].w] : nullptr;
    drawGauge(p, w, cat, L, hasSub, nameFont, rowX0 + col * L.cellW, y0 + row * L.cellH, st.epochNow);
  }
}
