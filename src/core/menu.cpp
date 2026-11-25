// ============================================================
//  menu.cpp — main menu and navigation
//
//  Three levels: main menu (section cards) -> section submenu
//  (screens with live numbers) -> screen. Sections are Section tables
//  and the menu treats them all alike.
// ============================================================
#include <ctype.h>
#include "core/lang.h"
#include "core/menu.h"
#include "core/theme.h"
#include "core/battery.h"
#include "core/settings.h"
#include "core/ui.h"
#include "core/langpick.h"
#include "core/board.h"
#include "gallery/gallery.h"
#include "web/web.h"
#include "pc/pc.h"
#include "ble/ble.h"
#include "wifi/wifi.h"
#include "subghz/subghz.h"
#include "audio/audio.h"
#include "probe/ir.h"
#include "probe/nfc.h"

// card order in the main menu
static const Section* const SECTIONS[] = { &bleSection, &wifiSection, &subghzSection, &soundSection, &irSection, &nfcSection, &webSection, &pcSection, &gallerySection, &settingsSection };
static const int N_SECTIONS = sizeof(SECTIONS) / sizeof(SECTIONS[0]);

static int  s_cur = -1;          // open section; -1 = main menu
static int  s_menuSel = 0;       // selected card
static bool s_sub = false;       // submenu is shown
static int  s_subSel = 0;        // submenu cursor

static const Section* cur() { return s_cur >= 0 ? SECTIONS[s_cur] : nullptr; }
bool navActive(const Section* s) { return cur() == s; }
bool navInSub() { return s_sub; }

void navBegin() { s_cur = -1; s_menuSel = 0; s_sub = false; }

static void enterSection(int i) {
  s_cur = i;
  const Section* s = cur();
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

// open a section from outside (e.g. the desktop app connected over USB: open Web)
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
  if (g_langAsk) { langPickClick(); return; }       // first boot: language first
  const Section* s = cur();
  if (!s)    { enterSection(s_menuSel); return; }
  if (s_sub) { s->open(s_subSel); s_sub = false; return; }
  if (s->click) s->click();
}

void navRotate(int dir) {
  if (g_langAsk) { langPickRotate(dir); return; }
  const Section* s = cur();
  if (!s)    { s_menuSel = (s_menuSel + dir + N_SECTIONS) % N_SECTIONS; return; }
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
    if (s->back && s->back()) return;      // the section closed something of its own (e.g. details)
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

// section caption -> big value + small caption ("23 devices" -> "23" / "DEVICES")
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

// HUD main menu: cut-corner panels, status on top, telemetry below
static void drawMainMenuHud() {
  int W = spr.width(), H = spr.height();
  uint16_t bg = C_BG(), acc = C_ACC(), steel = C(150, 150, 165), dim = C(110, 110, 125);
  spr.fillSprite(bg);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(dim);
  txt(L("// " DEVICE_NAME " · ONLINE", "// " DEVICE_NAME " · ГОТОВ"), 8, 5, 1);
  // battery: percent and five segments
  char buf[32];
  if (g_bat.ok) {
    snprintf(buf, sizeof(buf), L("BAT %d%%", "ЗАРЯД %d%%"), g_bat.soc);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(g_bat.soc <= 20 ? C_ALERT() : acc);
    txt(buf, W - 44, 5, 1);
    for (int i = 0; i < 5; i++) {
      bool on = g_bat.soc > i * 20;
      spr.fillRect(W - 40 + i * 7, 5, 5, 8, on ? (g_bat.soc <= 20 ? C_ALERT() : acc) : C(40, 40, 50));
    }
  }
  int span = W - 16;
  for (int s = 0; s < 16; s++) {                        // line fading to the right
    int x0 = 8 + s * span / 16, x1 = 8 + (s + 1) * span / 16;
    spr.drawFastHLine(x0, 18, x1 - x0, scale565(acc, 100 - s * 100 / 15));
  }

  // 5 cards visible; with more sections the strip scrolls with the selection
  const int VIS = min(N_SECTIONS, 5);
  const int gap = 5, cw = (W - 12 - gap * (VIS - 1)) / VIS, ch = 128, y0 = 24;
  int x0 = (W - (cw * VIS + gap * (VIS - 1))) / 2;
  static int first = 0;
  if (s_menuSel < first) first = s_menuSel;
  if (s_menuSel > first + VIS - 1) first = s_menuSel - VIS + 1;
  if (first > 0) spr.fillTriangle(1, y0 + ch / 2, 5, y0 + ch / 2 - 5, 5, y0 + ch / 2 + 5, acc);           // more on the left
  if (first + VIS < N_SECTIONS) spr.fillTriangle(W - 2, y0 + ch / 2, W - 6, y0 + ch / 2 - 5, W - 6, y0 + ch / 2 + 5, acc);
  for (int i = first; i < first + VIS; i++) {
    const Section* s = SECTIONS[i];
    bool sel = (i == s_menuSel);
    int x = x0 + (i - first) * (cw + gap), cx = x + cw / 2;
    uint16_t pbg = sel ? C_SELBG() : bg;
    if (sel) hudPanel(x, y0, cw, ch, 12, pbg, true, bg);
    hudPanel(x, y0, cw, ch, 12, sel ? acc : UI_LINE(), false, bg);
    s->icon(cx, y0 + 30, sel ? (themeKurenai() ? C_SELFG() : acc) : C(110, 110, 125), pbg);
    char title[40]; utf8Upper(LT(s->title), title, sizeof title);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(sel ? C_SELFG() : steel);
    txt(title, cx, y0 + 62, txtW(title, 2) <= cw - 4 ? 2 : txtW(title, 1) <= cw - 4 ? 1 : 8);   // long title: smaller font
    char sub[48], big[40], small[40];
    s->subtitle(sub, sizeof(sub));
    splitSubtitle(sub, big, small, sizeof(big));
    bool alertish = (strcmp(small, L("NO CHIP", "НЕТ ЧИПА")) == 0);
    spr.setTextColor(alertish ? C_ALERT() : (sel ? C_SELACC() : steel));
    { char bf[40]; fitText(big, bf, sizeof bf, cw - 4, 4); txt(bf, cx, y0 + 82, 4); }
    spr.setTextColor(sel && themeKurenai() ? UI_LINE() : dim);
    char sm[40]; fitText(small, sm, sizeof sm, cw - 4, 8);   // as much as fits (UTF-8 aware)
    txt(sm, cx, y0 + 108, 8);
  }

  // telemetry: uptime
  uint32_t t = millis() / 1000;
  snprintf(buf, sizeof(buf), "T+%02lu:%02lu:%02lu", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60));
  spr.setTextDatum(BL_DATUM);
  spr.setTextColor(dim);
  txt(L("CLICK: OPEN · SIDE: POWER", "НАЖАТЬ: ОТКРЫТЬ · БОК: ПИТАНИЕ"), 8, H - 4, 1);
  spr.setTextDatum(BR_DATUM);
  spr.setTextColor(acc);
  txt(buf, W - 8, H - 4, 1);
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
