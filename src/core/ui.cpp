// ============================================================
//  ui.cpp — design-system components
// ============================================================
#include "core/ui.h"
#include "core/lang.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/settings.h"

uint16_t UI_LABEL() { return themeKurenai() ? tft.color565(190, 186, 178) : tft.color565(150, 175, 192); }
uint16_t UI_DIM()   { return themeKurenai() ? tft.color565(138, 138, 138) : tft.color565(90, 112, 128); }
uint16_t UI_LINE()  { return themeKurenai() ? tft.color565(58, 58, 58)    : tft.color565(32, 50, 64); }
uint16_t UI_PANEL() { return themeKurenai() ? tft.color565(34, 34, 34)    : tft.color565(14, 42, 52); }

static void upper(const char* s, char* out, size_t n) { utf8Upper(s, out, n); }

void uiHeader(const char* title, const char* right, bool rightAlert) {
  int W = spr.width();
  char t[40], u[36];
  upper(title, u, sizeof(u));
  snprintf(t, sizeof(t), "// %s", u);
  spr.setTextDatum(TL_DATUM);
  if (themeKurenai()) {                            // KURENAI: title is a white cut-corner plate, black text
    snprintf(t, sizeof(t), "%s", u);
    int tw = txtW(t, 2), x0 = UI_M, x1 = UI_M + tw + 16;
    spr.fillTriangle(x0 + 4, 1, x1 + 4, 1, x0, 17, C_HOT());
    spr.fillTriangle(x1 + 4, 1, x1, 17, x0, 17, C_HOT());
    spr.setTextColor(C_BG());
    txt(t, UI_M + 8, 3, 2);
  } else {
    spr.setTextColor(C_ACC());
    txt(t, UI_M + 4, 3, 2);
  }
  if (right && right[0]) {
    char r0[48], r[48]; upper(right, r0, sizeof(r0));
    int room = W - UI_M - 4 - (UI_M + 4 + txtW(t, 2)) - 10;   // room to the right of the title
    fitText(r0, r, sizeof(r), max(room, 12), 1);               // don't overlap the title
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(rightAlert ? C_ALERT() : UI_LABEL());
    txt(r, W - UI_M - 4, 6, 1);
  }
  if (themeKurenai()) { spr.fillRect(UI_M, 19, W - 2 * UI_M, 2, C_ACC()); return; }   // KURENAI: solid red line
  // line under the header, fading to the right + origin diamond & terminal tick
  uint16_t a = C_ACC();
  int span = W - 2 * UI_M;
  for (int s = 0; s < 16; s++) {
    int x0 = UI_M + s * span / 16, x1 = UI_M + (s + 1) * span / 16;
    spr.drawFastHLine(x0, 19, x1 - x0, scale565(a, 100 - s * 85 / 15));
  }
  spr.fillRect(UI_M, 18, 3, 3, rightAlert ? C_ALERT() : a);
  spr.drawFastVLine(W - UI_M - 1, 17, 5, rightAlert ? C_ALERT() : scale565(a, 55));
}

void uiFooter(const char* left, const char* right) {
  int W = spr.width();
  char l[60] = "", r[60] = "";
  if (left && left[0]) upper(left, l, sizeof(l));
  if (right && right[0]) upper(right, r, sizeof(r));
  // doesn't fit: same font, one step smaller (same for both languages)
  int room = W - 2 * (UI_M + 8) - 10, sz = (txtW(l, 1) + txtW(r, 1) <= room) ? 1 : 8;
  spr.setTextColor(UI_DIM());
  if (l[0]) { spr.setTextDatum(BL_DATUM); txt(l, UI_M + 8, UI_FOOT_Y, sz); }
  if (r[0]) { spr.setTextDatum(BR_DATUM); txt(r, W - UI_M - 8, UI_FOOT_Y, sz); }
}

void uiFrame(int x, int y, int w, int h, uint16_t col) {
  const int L = 6;
  spr.drawFastHLine(x, y, L, col);             spr.drawFastVLine(x, y, L, col);
  spr.drawFastHLine(x + w - L, y, L, col);     spr.drawFastVLine(x + w - 1, y, L, col);
  spr.drawFastHLine(x, y + h - 1, L, col);     spr.drawFastVLine(x, y + h - L, L, col);
  spr.drawFastHLine(x + w - L, y + h - 1, L, col); spr.drawFastVLine(x + w - 1, y + h - L, L, col);
}

void uiReadout(int x, int y, const char* label, const char* value, uint16_t valCol, int valSize) {
  char l[32]; upper(label, l, sizeof(l));
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_DIM());
  txt(l, x, y, 1);
  spr.setTextColor(valCol);
  txt(value, x, y + 10, valSize);
}

void uiEmpty(const char* msg, const char* sub) {
  int W = spr.width(), cx = W / 2, cy = 62;
  uint16_t acc = C_ACC(), line = UI_LINE();
  // Calm Zen Ensō incomplete ring + cardinal ticks (static: zero SPI row churn when idle!)
  spr.drawArc(cx, cy, 20, 18, 30, 325, scale565(acc, 70), C_BG(), true);
  spr.drawFastHLine(cx - 26, cy, 4, line); spr.drawFastHLine(cx + 23, cy, 4, line);
  spr.drawFastVLine(cx, cy - 26, 4, line); spr.drawFastVLine(cx, cy + 23, 4, line);
  drawMarker(cx, cy, acc, false);
  char m[44]; upper(msg, m, sizeof(m));
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_HOT());
  txt(m, cx, 102, txtW(m, 4) <= W - 24 ? 4 : 2);
  if (sub) {
    char s[64]; upper(sub, s, sizeof(s));
    spr.setTextColor(UI_DIM());
    txt(s, cx, 124, txtW(s, 1) <= W - 16 ? 1 : 8);
  }
}

void uiRowHighlight(int y, int h) {
  int W = spr.width();
  hudPanel(UI_M - 4, y, W - 2 * UI_M + 8, h, 6, UI_PANEL(), true, C_BG());
  spr.fillRect(UI_M - 4, y + 2, 2, h - 8, C_ACC());
}
