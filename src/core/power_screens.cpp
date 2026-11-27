// ============================================================
//  power_screens.cpp — power screens: "keep holding" countdown ring and
//  the power-off animation. Split from power.cpp (hardware) so the
//  simulator can use them.
// ============================================================
#include "core/power.h"
#include "core/lang.h"
#include "core/board.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/settings.h"
#include "core/ui.h"

// arc around a circle (TFT_eSPI: 0° at the bottom, clockwise); may wrap past 360°
static void arcWrap(int cx, int cy, int r, int ir, int a0, int a1, uint16_t col, bool smooth) {
  if (a1 <= a0) return;
  if (a1 <= 360) { spr.drawArc(cx, cy, r, ir, a0, a1, col, C_BG(), smooth); return; }
  spr.drawArc(cx, cy, r, ir, a0, 360, col, C_BG(), smooth);
  spr.drawArc(cx, cy, r, ir, 0, min(a1 - 360, 360), col, C_BG(), smooth);
}

void drawPowerHold() {
  const int W = spr.width(), H = spr.height(), cx = W / 2, cy = H / 2 - 4;
  uint32_t p = g_powerHold > PWR_OFF_MS ? PWR_OFF_MS : g_powerHold;
  float k = constrain((float)(p - PWR_SHOW_MS) / (PWR_OFF_MS - PWR_SHOW_MS), 0.0f, 1.0f);
  spr.fillSprite(C_BG());
  // countdown ring: fills clockwise while the button is held
  const int R = 46;
  spr.drawArc(cx, cy, R, R - 7, 0, 360, UI_LINE(), C_BG(), false);
  if (k > 0) arcWrap(cx, cy, R, R - 7, 180, 180 + (int)(360 * k), k > 0.85f ? C_HOT() : C_ACC(), true);   // from 12 o'clock, clockwise
  for (int a = 0; a < 360; a += 30) {                  // ticks around the circle
    float rad = a * DEG_TO_RAD;
    spr.drawLine(cx + cosf(rad) * (R + 4), cy + sinf(rad) * (R + 4), cx + cosf(rad) * (R + 8), cy + sinf(rad) * (R + 8), a / 30 <= (int)(k * 12) ? C_ACC() : UI_LINE());
  }
  // power icon: arc with a gap at the top and a vertical bar
  uint16_t ic = k > 0.85f ? C_HOT() : C_ACC();
  arcWrap(cx, cy, 18, 14, 215, 505, ic, true);         // arc with a gap at the top
  spr.fillRoundRect(cx - 2, cy - 22, 5, 20, 2, ic);
  // labels
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACC());
  txt(L("// POWER", "// ПИТАНИЕ"), UI_M + 4, 8, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(UI_LABEL());
  char buf[16]; snprintf(buf, sizeof(buf), "%d%%", (int)(k * 100));
  txt(buf, W - UI_M - 4, 10, 1);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(UI_LABEL());
  txt(L("KEEP HOLDING THE SIDE BUTTON", "ДЕРЖИТЕ БОКОВУЮ КНОПКУ"), cx, H - 18, 1);
  present();
}

// ---------- power-off animation (~1.4 s): old-TV style ----------
// radar rings collapse to the center, the picture squeezes into a bright line,
// the line into a dot, the dot fades. shutFrame(t) draws the frame at t (simulator too).
void shutFrame(uint32_t t) {
  const int W = spr.width(), H = spr.height(), cx = W / 2, cy = H / 2;
  spr.fillSprite(C_BG());
  if (t < 650) {                                       // 1. rings converge, text flickers
    float k = t / 650.0f;
    for (int i = 0; i < 4; i++) {
      int r = (int)((20 + i * 20) * (1 - k * k));
      if (r > 1) spr.drawCircle(cx, cy, r, i == 3 ? C_ACC() : UI_DIM());
    }
    for (int a = 0; a < 360; a += 30) {                // ray ticks retract too
      float rad = a * DEG_TO_RAD, r0 = 86 * (1 - k), r1 = r0 + 6;
      spr.drawLine(cx + cosf(rad) * r0, cy + sinf(rad) * r0, cx + cosf(rad) * r1, cy + sinf(rad) * r1, UI_DIM());
    }
    spr.fillCircle(cx, cy, 2 + (int)(k * 4), C_HOT());
    if ((t / 90) % 4 != 0) {
      spr.setTextDatum(MC_DATUM);
      spr.setTextColor(C_ACC());
      txt(L("SHUTTING DOWN", "ВЫКЛЮЧЕНИЕ"), cx, cy + 40, 1);
      spr.setTextColor(UI_LABEL());
      txt(DEVICE_NAME, cx, cy - 42, 1);
    }
  } else if (t < 900) {                                // 2. screen squeezes into a glowing line
    float k = (t - 650) / 250.0f;
    int h = max(2, (int)(H * (1 - k) * (1 - k)));
    spr.fillRect(0, cy - h / 2, W, h, scale565(C_ACC(), 35 + (int)(k * 30)));
    spr.fillRect(0, cy - max(1, h / 6), W, max(2, h / 3), C_HOT());
  } else if (t < 1150) {                               // 3. line shrinks to a dot
    float k = (t - 900) / 250.0f;
    int w = max(4, (int)(W * (1 - k) * (1 - k)));
    spr.fillRect(cx - w / 2, cy - 1, w, 3, C_HOT());
    spr.drawFastHLine(cx - w / 2 - 6, cy, w + 12, C_ACC());
  } else {                                             // 4. dot fades
    float k = min(1.0f, (t - 1150) / 250.0f);
    int r = max(1, (int)(4 * (1 - k)));
    spr.fillCircle(cx, cy, r + 2, scale565(C_ACC(), (int)(60 * (1 - k))));
    spr.fillCircle(cx, cy, r, scale565(C_HOT(), (int)(100 * (1 - k))));
  }
}

// ---------- Evil Mode / Emergency IR Shutdown screen ----------
void evilFrame(uint32_t elapsed, uint32_t total) {
  const int W = spr.width(), H = spr.height(), cx = W / 2;
  uint32_t rem = elapsed < total ? total - elapsed : 0;
  float pct = total ? (float)rem / (float)total : 0.0f;
  bool panic = rem < 1200;
  bool invert = panic && ((elapsed / 70) & 1);

  uint16_t red    = C(255, 20, 20);
  uint16_t darkR  = C(90, 0, 0);
  uint16_t bg     = invert ? C(160, 0, 0) : TFT_BLACK;
  uint16_t fg     = invert ? TFT_BLACK : red;
  uint16_t bright = invert ? TFT_WHITE : C(255, 190, 190);

  spr.fillSprite(bg);

  // Scrolling diagonal hazard stripes top & bottom
  int shift = (int)((elapsed / 18) % 20);
  for (int x = -20; x < W + 20; x += 20) {
    for (int k = 0; k < 10; k++) {
      spr.drawLine(x + shift + k, 0, x + shift + k - 12, 13, fg);
      spr.drawLine(x - shift + k, H - 14, x - shift + k - 12, H - 1, fg);
    }
  }
  spr.drawFastHLine(0, 14, W, red);
  spr.drawFastHLine(0, H - 15, W, red);

  // Subtle horizontal glitch scanlines
  uint32_t seed = elapsed * 2654435761u;
  for (int i = 0; i < 4; i++) {
    seed = seed * 1664525u + 1013904223u;
    int gy = 20 + (int)(seed % (H - 40));
    int gx = (int)((seed >> 8) % (W - 40));
    int gw = 20 + (int)((seed >> 16) % 70);
    spr.drawFastHLine(gx, gy, min(gw, W - gx), darkR);
  }

  // Header alert badge
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(((elapsed / 120) & 1) ? bright : fg);
  txt(L("!! EVIL MODE !!", "!! ЗЛОЙ РЕЖИМ !!"), cx, 32, 2);

  // Huge shaking countdown
  int jx = (int)((seed % 5) - 2) * (panic ? 2 : 1);
  int jy = (int)(((seed >> 4) % 5) - 2) * (panic ? 2 : 1);
  char buf[20];
  snprintf(buf, sizeof(buf), "%lu.%02lu", (unsigned long)(rem / 1000), (unsigned long)((rem % 1000) / 10));
  spr.setTextColor(bright);
  txt(buf, cx + jx, 78 + jy, 4);

  // Draining progress bar
  int bx = 24, by = 114, bw = W - 48, bh = 10;
  spr.drawRect(bx, by, bw, bh, red);
  int fw = (int)((bw - 4) * pct);
  if (fw > 0) spr.fillRect(bx + 2, by + 2, fw, bh - 4, fg);

  // Subtitle
  spr.setTextColor(fg);
  txt(L("EMERGENCY SHUTDOWN", "ЭКСТРЕННОЕ ОТКЛЮЧЕНИЕ"), cx, 138, 1);
}

