// ============================================================
//  display.cpp — screen and shared UI building blocks
// ============================================================
#include "core/display.h"
#include "core/lang.h"
#include "core/theme.h"
#include "core/ui.h"

TFT_eSPI    tft;
TFT_eSprite spr = TFT_eSprite(&tft);

void displayBegin() {
  // backlight goes on only after the screen is cleared, otherwise garbage
  // from display memory (often red) flashes for a moment
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, LOW);
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(C_BG());
  delay(20);
  digitalWrite(TFT_BL, HIGH);
  spr.setColorDepth(16);
  if (!spr.createSprite(tft.width(), tft.height())) {   // out of memory: fall back to 8 bpp
    spr.setColorDepth(8);
    spr.createSprite(tft.width(), tft.height());
  }
}


bool g_fastFrame = false;

void macStr(const uint8_t* m, char* out) {   // m = least significant byte first
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[5], m[4], m[3], m[2], m[1], m[0]);
}

// shorten to px width (font), appending ".."
void fitText(const char* src, char* dst, size_t cap, int px, int font) {
  if (!cap) return;
  strncpy(dst, src, cap - 1); dst[cap - 1] = 0;
  size_t n = strlen(dst);
  if (strlen(src) > n) {                                      // copy truncated it: don't leave half a UTF-8 character
    size_t k = n;
    while (k && ((uint8_t)dst[k - 1] & 0xC0) == 0x80) k--;    // k-1 = start of the last character
    if (k) {
      uint8_t lead = (uint8_t)dst[k - 1];
      size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
      if (n - (k - 1) < need) n = k - 1;
    }
    dst[n] = 0;
  }
  if (txtW(dst, font) <= px) return;
  if (cap < 4) { dst[0] = 0; return; }
  while (n > 1) {
    n--;                                                      // drop the whole last character (UTF-8)
    while (n > 0 && ((uint8_t)dst[n] & 0xC0) == 0x80) n--;
    dst[n] = 0;
    if (n + 2 >= cap) continue;                               // ensure room for ".." + NUL inside dst[cap]
    char tmp[96]; snprintf(tmp, sizeof(tmp), "%s..", dst);
    if (txtW(tmp, font) <= px) { strlcpy(dst, tmp, cap); return; }
  }
}

// 4 signal-level bars
void drawBars(int x, int y, int rssi, uint16_t col) {
  int lvl = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : rssi > -90 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 3;
    spr.fillRect(x + i * 6, y + 12 - h, 4, h, i < lvl ? col : C(45, 45, 55));
  }
}

void drawSpark(int gx, int gy, int gw, int gh, const int8_t* buf, int n, int cap, uint16_t col) {
  int px = -1, py = -1;
  for (int i = 0; i < n; i++) {
    int v = buf[i];
    int xx = gx + 1 + (gw - 3) * (i + (cap - n)) / (cap - 1);
    if (v <= -127) { px = -1; continue; }
    int c = constrain(v, -100, -30);
    int yy = gy + gh - 2 - (c + 100) * (gh - 4) / 70;
    if (px >= 0) spr.drawLine(px, py, xx, yy, col);
    else         spr.drawPixel(xx, yy, col);
    px = xx; py = yy;
  }
}

void drawSubmenu(const char* title, uint16_t acc, SubItem* it, int n, int sel) {
  int W = spr.width();
  spr.fillSprite(C_BG());
  uiHeader(title, L("click: open", "нажать: открыть"));
  int rh = n > 5 ? 18 : 25, y0 = UI_TOP + 1;
  // Spring-smoothed selection bar: animates at 60 fps while moving, settles to 0 SPI rows when still
  static float s_subY = -1.0f;
  static const char* s_lastTitle = nullptr;
  float targetY = (float)(y0 + sel * rh);
  if (s_lastTitle != title || s_subY < 0) { s_subY = targetY; s_lastTitle = title; }
  else {
    float d = targetY - s_subY;
    if (fabsf(d) > 0.45f) { s_subY += d * 0.36f; g_fastFrame = true; }
    else s_subY = targetY;
  }
  int sy = (int)lroundf(s_subY);
  if (themeKurenai()) {
    int x0 = UI_M - 4, x1 = W - UI_M + 4, h = rh - 2;
    spr.fillTriangle(x0 + 5, sy, x1, sy, x0, sy + h, C_HOT());
    spr.fillTriangle(x1, sy, x1 - 5, sy + h, x0, sy + h, C_HOT());
    spr.fillRect(x0 + 2, sy + 3, 3, h - 6, C_ACC());
  } else {
    uiRowHighlight(sy, rh - 2);
  }
  for (int i = 0; i < n; i++) {
    int y = y0 + i * rh, cy = y + (rh - 2) / 2;
    bool isSel = (abs(y - sy) < rh / 2);
    bool plate = isSel && themeKurenai();
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(plate ? C_BG() : isSel ? C_HOT() : UI_LABEL());
    txt(it[i].name, UI_M + 8, cy + 1, n > 5 ? 2 : 4);
    char d[64]; utf8Upper(it[i].desc, d, sizeof d);
    spr.setTextColor(plate ? UI_LINE() : isSel ? UI_LABEL() : UI_DIM());
    txt(d, 118, cy + 1, 1);
    char v[24]; fitText(it[i].value, v, sizeof(v), 84, 2);
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(plate ? C_ACC() : isSel ? acc : C_HOT());
    txt(v, W - UI_M - 8, cy + 1, 2);
  }
  uiFooter(L("wheel: choose", "колесо: выбор"), L("side: back", "кнопка: назад"));
  present();
}

void hudPanel(int x, int y, int w, int h, int cut, uint16_t col, bool filled, uint16_t bg) {
  if (filled) {
    spr.fillRect(x, y, w, h, col);
    spr.fillTriangle(x + w - cut, y, x + w, y, x + w, y + cut, bg);   // cut top right
    spr.fillTriangle(x, y + h - cut, x, y + h, x + cut, y + h, bg);   // cut bottom left
    return;
  }
  spr.drawFastHLine(x, y, w - cut, col);
  spr.drawLine(x + w - cut, y, x + w - 1, y + cut, col);
  spr.drawFastVLine(x + w - 1, y + cut, h - cut, col);
  spr.drawFastHLine(x + cut, y + h - 1, w - cut, col);
  spr.drawLine(x, y + h - cut, x + cut, y + h - 1, col);
  spr.drawFastVLine(x, y, h - cut, col);
}

void drawArcGauge(int cx, int cy, int r, int thick, int pct, uint16_t col, uint16_t track, uint16_t bg) {
  pct = constrain(pct, 0, 100);
  // drawArc: 0° is at the bottom, clockwise; top semicircle = 90°..270°
  spr.drawArc(cx, cy, r, r - thick, 90, 270, track, bg, false);
  int a1 = 90 + 180 * pct / 100;
  if (a1 > 90) spr.drawArc(cx, cy, r, r - thick, 90, a1, col, bg, false);
  for (int k = 0; k <= 12; k++) {                         // ticks outside
    float a = PI + k * PI / 12;
    int r0 = r + 3, r1 = r + (k % 3 == 0 ? 9 : 6);
    spr.drawLine(cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r1, cy + sinf(a) * r1, k * 100 / 12 <= pct ? col : track);
  }
}

// --- main menu icons ---
void iconBluetooth(int x, int y, uint16_t c, uint16_t bg) {
  float w = 3.5f;
  spr.drawWideLine(x, y - 22, x, y + 22, w, c, bg);
  spr.drawWideLine(x, y - 22, x + 12, y - 11, w, c, bg);
  spr.drawWideLine(x + 12, y - 11, x - 11, y + 10, w, c, bg);
  spr.drawWideLine(x, y + 22, x + 12, y + 11, w, c, bg);
  spr.drawWideLine(x + 12, y + 11, x - 11, y - 10, w, c, bg);
}
void iconWifi(int x, int y, uint16_t c, uint16_t bg) {
  int cy = y + 16;
  for (int i = 0; i < 3; i++) {
    int r = 12 + i * 11;
    spr.drawArc(x, cy, r, r - 5, 135, 225, c, bg, true);   // arcs facing up
  }
  spr.fillSmoothCircle(x, cy - 1, 4, c, bg);
}
void iconGear(int x, int y, uint16_t c, uint16_t bg) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    spr.drawWideLine(x + cos(a) * 14, y + sin(a) * 14, x + cos(a) * 22, y + sin(a) * 22, 7, c, bg);
  }
  spr.fillSmoothCircle(x, y, 17, c, bg);
  spr.fillSmoothCircle(x, y, 7, bg, c);
}
// antenna with waves (Sub-GHz)
void iconAntenna(int x, int y, uint16_t c, uint16_t bg) {
  spr.drawWideLine(x, y - 8, x, y + 22, 3.5f, c, bg);
  spr.drawWideLine(x, y + 2, x - 9, y + 22, 3, c, bg);
  spr.drawWideLine(x, y + 2, x + 9, y + 22, 3, c, bg);
  spr.fillSmoothCircle(x, y - 10, 4, c, bg);
  for (int i = 0; i < 2; i++) {
    int r = 11 + i * 8;
    spr.drawArc(x, y - 10, r, r - 3, 225, 315, c, bg, true);    // waves left
    spr.drawArc(x, y - 10, r, r - 3, 45, 135, c, bg, true);     // waves right
  }
}

// microphone (Sound)
void iconMic(int x, int y, uint16_t c, uint16_t bg) {
  spr.fillSmoothRoundRect(x - 7, y - 20, 14, 26, 7, c, bg);
  spr.drawArc(x, y - 2, 13, 10, 270, 90, c, bg, true);   // horseshoe under the capsule
  spr.drawWideLine(x, y + 12, x, y + 20, 3, c, bg);
  spr.drawWideLine(x - 8, y + 21, x + 8, y + 21, 3, c, bg);
}

// cube in a cube (Gallery)
void iconCube(int x, int y, uint16_t c, uint16_t bg) {
  const int o = 14, i = 7, d = 5;
  spr.drawRect(x - o - d / 2, y - o + d / 2, 2 * o, 2 * o, c);
  spr.drawRect(x - o + d / 2 + 1, y - o - d / 2, 2 * o, 2 * o, c);
  spr.drawRect(x - i, y - i, 2 * i, 2 * i, c);
  spr.drawLine(x - o - d / 2, y - o + d / 2, x - i, y - i, c);
  spr.drawLine(x + o - d / 2 - 1, y + o + d / 2 - 1, x + i - 1, y + i - 1, c);
  spr.drawLine(x + o + d / 2, y - o - d / 2, x + i - 1, y - i, c);
  spr.drawLine(x - o + d / 2 + 1, y + o - d / 2 - 1, x - i, y + i - 1, c);
  (void)bg;
}

// phone with waves (Web)
void iconPhone(int x, int y, uint16_t c, uint16_t bg) {
  spr.drawSmoothRoundRect(x - 9, y - 17, 3, 2, 18, 34, c, bg);
  spr.drawFastHLine(x - 3, y + 12, 6, c);
  spr.drawArc(x + 8, y - 13, 7, 5, 180, 270, c, bg, true);
  spr.drawArc(x + 8, y - 13, 12, 10, 180, 270, c, bg, true);
}

// laptop (PC)
void iconLaptop(int x, int y, uint16_t c, uint16_t bg) {
  spr.drawRoundRect(x - 17, y - 16, 34, 23, 2, c);
  spr.fillRect(x - 13, y - 12, 26, 15, c);
  spr.fillRect(x - 11, y - 10, 22, 11, bg);
  spr.drawLine(x - 9, y - 1, x - 4, y - 6, c); spr.drawLine(x - 4, y - 6, x + 1, y - 3, c); spr.drawLine(x + 1, y - 3, x + 8, y - 9, c);
  spr.fillRoundRect(x - 22, y + 9, 44, 5, 2, c);
}
