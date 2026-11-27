// ============================================================
//  menu.cpp — main menu and navigation
//
//  Three levels: main menu (Evangelion MAGI × Zen 天眼 focal stage)
//  -> section submenu (screens with live numbers) -> screen.
// ============================================================
#include <ctype.h>
#include <math.h>
#include "core/lang.h"
#include "core/menu.h"
#include "core/theme.h"
#include "core/battery.h"
#include "core/settings.h"
#include "core/ui.h"
#include "core/langpick.h"
#include "core/board.h"
#include "core/leds.h"
#include "core/seal.h"
#include "gallery/gallery.h"
#include "web/web.h"
#include "pc/pc.h"
#include "ble/ble.h"
#include "wifi/wifi.h"
#include "subghz/subghz.h"
#include "audio/audio.h"
#include "probe/ir.h"
#include "probe/nfc.h"
#include "locator/locator.h"

// combined IR · NFC section so the main menu stays compact
static int s_probeScr = 0;
static void probeSubtitle(char* out, size_t n) {
  if (g_ir.have) snprintf(out, n, "IR:%s", g_ir.last.proto);
  else if (g_nfc.taps) snprintf(out, n, "%lu NFC", (unsigned long)g_nfc.taps);
  else snprintf(out, n, "IR / NFC");
}
static void probeEnter() {}
static void probeLeave() { irStop(); nfcStop(); }
static void probeScreenInfo(int i, SubItem& it) {
  if (i == 0) {
    it.name = L("IR Remote", "ИК-пульт");
    it.desc = L("38 kHz receiver", "приёмник 38 кГц");
    irSection.subtitle(it.value, sizeof(it.value));
  } else {
    it.name = L("NFC Card", "NFC-карта");
    it.desc = L("13.56 MHz reader", "считыватель 13.56 МГц");
    nfcSection.subtitle(it.value, sizeof(it.value));
  }
}
static int  probeScreenNow() { return s_probeScr; }
static void probeOpen(int i) {
  s_probeScr = i ? 1 : 0;
  if (s_probeScr == 0) { nfcStop(); irStart(); }
  else                 { irStop(); nfcStart(); }
}
static void probeClose() { irStop(); nfcStop(); }
static void probeClick() { if (s_probeScr == 0) irSection.click(); else nfcSection.click(); }
static void probeRotate(int d) { if (s_probeScr == 0 && irSection.rotate) irSection.rotate(d); else if (s_probeScr == 1 && nfcSection.rotate) nfcSection.rotate(d); }
static void probeTick(uint32_t now) { if (s_probeScr == 0) irSection.tick(now); else nfcSection.tick(now); }
static void probeDraw(uint32_t now) { if (s_probeScr == 0) irSection.draw(now); else nfcSection.draw(now); }

static const Section probeSection = {
  LP("IR·NFC", "ИК·NFC"), 200, 140, 220, iconRemote, probeSubtitle,
  probeEnter, probeLeave,
  2, probeScreenInfo, probeScreenNow, probeOpen, probeClose,
  probeClick, probeRotate, nullptr, probeTick, probeDraw,
};

static const Section* const SECTIONS[] = {
  &bleSection, &locatorSection, &wifiSection, &webSection, &pcSection,
  &gallerySection, &subghzSection, &soundSection, &probeSection, &settingsSection
};
static const int N_SECTIONS = sizeof(SECTIONS) / sizeof(SECTIONS[0]);

static int   s_cur = -1;          // open section; -1 = main menu
static int   s_menuSel = 0;       // selected card
static float s_glide = 0.0f;      // horizontal spring offset on rotation (px)
static bool  s_sub = false;       // submenu is shown
static int   s_subSel = 0;        // submenu cursor

static const Section* cur() { return s_cur >= 0 ? SECTIONS[s_cur] : nullptr; }
bool navActive(const Section* s) { return cur() == s; }
bool navInSub() { return s_sub; }

void navBegin() { s_cur = -1; s_menuSel = 0; s_glide = 0.0f; s_sub = false; }

static void enterSection(int i) {
  s_cur = i;
  const Section* s = cur();
  ledsEvent(2);
  s->enter();
  s_sub = s->nScreens > 0;
  if (s_sub) s_subSel = s->screenNow();
}

static void goMain() {
  const Section* s = cur();
  if (s) s->leave();
  s_menuSel = s_cur;
  s_cur = -1;
  s_sub = false;
}

void navOpen(const Section* target) {
  if (cur() == target) return;
  int idx = -1;
  for (int i = 0; i < N_SECTIONS; i++) if (SECTIONS[i] == target) idx = i;
  if (idx < 0) return;
  if (cur()) goMain();
  enterSection(idx);
  if (s_sub) { target->open(s_subSel); s_sub = false; }
}

void navClick() {
  if (g_langAsk) { langPickClick(); return; }
  const Section* s = cur();
  if (!s)    { enterSection(s_menuSel); return; }
  if (s_sub) { s->open(s_subSel); s_sub = false; return; }
  if (s->click) s->click();
}

void navRotate(int dir) {
  if (g_langAsk) { langPickRotate(dir); return; }
  const Section* s = cur();
  if (!s) {
    s_menuSel = (s_menuSel + dir + N_SECTIONS) % N_SECTIONS;
    s_glide = constrain(s_glide + (dir > 0 ? 28.0f : -28.0f), -44.0f, 44.0f);
    return;
  }
  if (s_sub) { s_subSel = (s_subSel + dir + s->nScreens) % s->nScreens; return; }
  if (s->rotate) s->rotate(dir);
}

void navToSub() {
  const Section* s = cur();
  if (!s || s->nScreens == 0) return;
  s_subSel = s->screenNow();
  s->close();
  s_sub = true;
}

void navBack() {
  if (g_langAsk) return;
  const Section* s = cur();
  if (!s) return;
  if (!s_sub && s->nScreens > 0) {
    if (s->back && s->back()) return;
    navToSub();
    return;
  }
  goMain();
}

void navTick(uint32_t now) {
  const Section* s = cur();
  if (s && s->tick) s->tick(now);
}

// ---------- drawing ----------
static void drawSectionSub(const Section* s) {
  SubItem items[8];
  int n = min(s->nScreens, 8);
  for (int i = 0; i < n; i++) s->screenInfo(i, items[i]);
  char title[40]; utf8Upper(LT(s->title), title, sizeof title);
  drawSubmenu(title, C(s->r, s->g, s->b), items, n, s_subSel);
}

static void splitSubtitle(const char* sub, char* big, char* small, size_t n) {
  const char* colon = strchr(sub, ':');
  if (isdigit((unsigned char)sub[0])) {
    const char* sp = strchr(sub, ' ');
    size_t l = sp ? (size_t)(sp - sub) : strlen(sub);
    snprintf(big, n, "%.*s", (int)l, sub);
    snprintf(small, n, "%s", sp ? sp + 1 : "");
  } else if (colon) {
    snprintf(small, n, "%.*s", (int)(colon - sub), sub);
    const char* v = colon + 1; while (*v == ' ') v++;
    snprintf(big, n, "%s", v);
  } else {
    snprintf(big, n, "--");
    snprintf(small, n, "%s", sub);
  }
  utf8UpperInPlace(big); utf8UpperInPlace(small);
}

// Crisp 2x-downsampled 16x30 天眼 (TENGEN) Hanko stamp from core/seal.h
static void drawMiniSeal(int x0, int y0, uint16_t ink, uint16_t bg) {
  for (int y = 0; y < 30; y++) {
    for (int x = 0; x < 16; x++) {
      bool cut = false;
      for (int dy = 0; dy < 2; dy++) {
        int sy = y * 2 + dy;
        for (int dx = 0; dx < 2; dx++) {
          int sx = x * 2 + dx;
          uint8_t b = SEAL_BITS[sy * 4 + (sx >> 3)];
          if (!((b >> (7 - (sx & 7))) & 1)) cut = true;
        }
      }
      spr.drawPixel(x0 + x, y0 + y, cut ? bg : ink);
    }
  }
}

// Sacred Zen Enso (incomplete brush circle) + 8-spoke Dharma compass halo
static void drawEnsoHalo(int cx, int cy, int r, uint16_t acc, uint16_t dim) {
  // 8 subtle Dharma ticks around the outer perimeter
  for (int k = 0; k < 8; k++) {
    float a = k * PI * 0.25f;
    int x0 = cx + (int)lroundf(cosf(a) * (r + 2));
    int y0 = cy + (int)lroundf(sinf(a) * (r + 2));
    int x1 = cx + (int)lroundf(cosf(a) * (r + (k % 2 == 0 ? 5 : 3)));
    int y1 = cy + (int)lroundf(sinf(a) * (r + (k % 2 == 0 ? 5 : 3)));
    spr.drawLine(x0, y0, x1, y1, k % 2 == 0 ? acc : dim);
  }
  // Incomplete Enso arc (leaves organic 52-degree Zen opening at bottom-right)
  int px = -1, py = -1;
  for (int d = 42; d <= 350; d += 8) {
    float a = d * PI / 180.0f;
    int x = cx + (int)lroundf(cosf(a) * r);
    int y = cy + (int)lroundf(sinf(a) * r);
    if (px >= 0) {
      spr.drawLine(px, py, x, y, d < 280 ? acc : dim);
      if (d > 75 && d < 220) {
        int xi = cx + (int)lroundf(cosf(a) * (r - 1));
        int yi = cy + (int)lroundf(sinf(a) * (r - 1));
        spr.drawPixel(xi, yi, acc);
      }
    }
    px = x; py = y;
  }
}

// Flanking wing card (prev / next section)
static void drawWingCard(int x, int y, int w, int h, const Section* s, bool isLeft, uint16_t bg, uint16_t lineCol, uint16_t dim, uint16_t labelCol, uint16_t acc) {
  hudPanel(x, y, w, h, 9, lineCol, false, bg);
  int cx = x + w / 2;
  // Subtle NERV directional chevron at outer edge
  int ax = isLeft ? x + 5 : x + w - 5, ay = y + h / 2;
  int dx = isLeft ? 3 : -3;
  spr.drawLine(ax, ay, ax + dx, ay - 4, acc);
  spr.drawLine(ax, ay, ax + dx, ay + 4, acc);

  s->icon(cx, y + 26, dim, bg);

  char title[36], fitT[36];
  utf8Upper(LT(s->title), title, sizeof title);
  fitText(title, fitT, sizeof fitT, w - 10, 1);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(labelCol);
  txt(fitT, cx, y + 50, 1);

  spr.drawFastHLine(x + 10, y + 63, w - 20, lineCol);

  char sub[48], big[36], small[36], fitB[24], fitS[24];
  s->subtitle(sub, sizeof(sub));
  splitSubtitle(sub, big, small, sizeof(big));
  bool alertish = (strcmp(small, L("NO CHIP", "НЕТ ЧИПА")) == 0);
  fitText(big, fitB, sizeof fitB, w - 10, 2);
  spr.setTextColor(alertish ? C_ALERT() : labelCol);
  txt(fitB, cx, y + 68, 2);

  fitText(small, fitS, sizeof fitS, w - 10, 1);
  spr.setTextColor(dim);
  txt(fitS, cx, y + 86, 1);
}

// Evangelion MAGI × Zen 天眼 Focal Altar Main Menu (320x170)
static void drawMainMenuHud() {
  int W = spr.width(), H = spr.height();
  uint16_t bg = C_BG(), acc = C_ACC(), hot = C_HOT(), lineCol = UI_LINE(), labelCol = UI_LABEL(), dim = UI_DIM();
  spr.fillSprite(bg);

  // Decay spring glide smoothly; zero SPI churn once settled!
  if (fabsf(s_glide) > 0.4f) {
    s_glide *= 0.60f;
    g_fastFrame = true;
  } else {
    s_glide = 0.0f;
  }
  int gx = (int)lroundf(s_glide);

  // ---- Top NERV Command Bar ----
  for (int k = 0; k < 3; k++) {
    spr.drawLine(8 + k * 4, 13, 11 + k * 4, 5, acc);
  }
  char buf[40];
  snprintf(buf, sizeof(buf), L("TENGEN · MAGI  %02d/%02d", "TENGEN · MAGI  %02d/%02d"), s_menuSel + 1, N_SECTIONS);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(hot);
  txt(buf, 23, 5, 1);

  if (g_bat.ok) {
    uint16_t bc = g_bat.soc <= 20 ? C_ALERT() : acc;
    snprintf(buf, sizeof(buf), "%d%%", g_bat.soc);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(bc);
    txt(buf, W - 44, 5, 1);
    for (int i = 0; i < 5; i++) {
      bool on = g_bat.soc > i * 20;
      spr.fillRect(W - 40 + i * 7, 5, 5, 8, on ? bc : lineCol);
    }
  }

  // Fading tactical rule + 10 section pips
  int span = W - 16;
  for (int s = 0; s < 16; s++) {
    int x0 = 8 + s * span / 16, x1 = 8 + (s + 1) * span / 16;
    spr.drawFastHLine(x0, 18, x1 - x0, scale565(acc, 100 - s * 88 / 15));
  }
  spr.fillRect(8, 17, 3, 3, acc);
  for (int p = 0; p < N_SECTIONS; p++) {
    int px = W / 2 - (N_SECTIONS * 6) / 2 + p * 6;
    spr.fillRect(px, 20, p == s_menuSel ? 4 : 2, 2, p == s_menuSel ? acc : lineCol);
  }

  // ---- Left & Right Flanking Wings (prev / next sections) ----
  int prevIdx = (s_menuSel - 1 + N_SECTIONS) % N_SECTIONS;
  int nextIdx = (s_menuSel + 1) % N_SECTIONS;
  drawWingCard(6 + gx / 3, 35, 72, 102, SECTIONS[prevIdx], true, bg, lineCol, dim, labelCol, acc);
  drawWingCard(W - 78 + gx / 3, 35, 72, 102, SECTIONS[nextIdx], false, bg, lineCol, dim, labelCol, acc);

  // ---- Central Sacred Focal Altar (active section) ----
  const Section* s = SECTIONS[s_menuSel];
  int cw = 152, ch = 124, xC = (W - cw) / 2 + gx, yC = 25, cx = xC + cw / 2;
  uint16_t pbg = C_SELBG(), fg = C_SELFG(), sAcc = C_SELACC();
  uint16_t haloDim = themeKurenai() ? scale565(sAcc, 45) : lineCol;

  hudPanel(xC, yC, cw, ch, 13, pbg, true, bg);
  hudPanel(xC, yC, cw, ch, 13, acc, false, bg);
  if (!themeKurenai()) spr.fillRect(xC + 14, yC + 2, 48, 2, acc);

  // Inner NERV tactical corner reticles
  spr.drawFastHLine(xC + 6, yC + ch - 6, 8, sAcc);
  spr.drawFastVLine(xC + 6, yC + ch - 14, 8, sAcc);
  spr.drawFastHLine(xC + cw - 14, yC + ch - 6, 8, sAcc);
  spr.drawFastVLine(xC + cw - 7, yC + ch - 14, 8, sAcc);

  // Section index tag (top-left of altar) & authentic 16x30 天眼 Hanko stamp (top-right)
  snprintf(buf, sizeof(buf), "SEC·%02d", s_menuSel + 1);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(themeKurenai() ? sAcc : dim);
  txt(buf, xC + 10, yC + 8, 1);
  drawMiniSeal(xC + cw - 24, yC + 7, C_SEAL(), pbg);

  // Enso + Dharma Compass Halo around active icon
  drawEnsoHalo(cx, yC + 30, 19, sAcc, haloDim);
  s->icon(cx, yC + 30, themeKurenai() ? fg : acc, pbg);

  // Active Section Title
  char title[40], fitT[40];
  utf8Upper(LT(s->title), title, sizeof title);
  int tFont = txtW(title, 2) <= cw - 20 ? 2 : 1;
  fitText(title, fitT, sizeof fitT, cw - 16, tFont);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(fg);
  txt(fitT, cx, yC + 58, tFont);

  // Subtle horizontal altar hairline with center diamond
  spr.drawFastHLine(xC + 18, yC + 75, cw - 36, haloDim);
  spr.fillRect(cx - 1, yC + 74, 3, 3, sAcc);

  // Live Hero Metric + Unit Caption
  char sub[48], big[40], small[40], bf[40], sm[40];
  s->subtitle(sub, sizeof(sub));
  splitSubtitle(sub, big, small, sizeof(big));
  bool alertish = (strcmp(small, L("NO CHIP", "НЕТ ЧИПА")) == 0);
  fitText(big, bf, sizeof bf, cw - 16, 4);
  spr.setTextColor(alertish ? C_ALERT() : sAcc);
  txt(bf, cx, yC + 80, 4);

  fitText(small, sm, sizeof sm, cw - 16, 1);
  spr.setTextColor(themeKurenai() ? fg : labelCol);
  txt(sm, cx, yC + 108, 1);

  // ---- Bottom Tactical Footer ----
  uint32_t t = millis() / 1000;
  snprintf(buf, sizeof(buf), "T+%02lu:%02lu:%02lu", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60));
  spr.setTextDatum(BL_DATUM);
  spr.setTextColor(dim);
  txt(L("WHEEL: SELECT · CLICK: OPEN", "КОЛЕСО: ВЫБОР · НАЖАТЬ: ВХОД"), 8, H - 3, 1);
  spr.setTextDatum(BR_DATUM);
  spr.setTextColor(acc);
  txt(buf, W - 8, H - 3, 1);
  present();
}

static void drawMainMenu() { drawMainMenuHud(); }

void navDraw(uint32_t now) {
  if (g_langAsk) { langPickDraw(now); return; }
  const Section* s = cur();
  if (!s)         drawMainMenu();
  else if (s_sub) drawSectionSub(s);
  else {
    if (s->nScreens > 1 && s->screenNow) { g_uiPages = s->nScreens; g_uiPage = s->screenNow(); }
    s->draw(now);
    g_uiPages = 0;
  }
}
