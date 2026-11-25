// ============================================================
//  subghz_screens.cpp — Sub-GHz screens: SPECTRUM · MONITOR · SENSORS
//  All screens are built from core/ui.h components on a common grid.
// ============================================================
#include "subghz/subghz_model.h"
#include "core/lang.h"
#include "subghz/sensors.h"
#include "core/timeutil.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"

// waterfall: v = dBm + 120 (0..110); -110 almost black … -40 white (cached 111-entry LUT per theme)
static uint16_t wfColor(int v) {
  static uint16_t s_wfLut[111];
  static int s_wfTheme = -1;
  int curTheme = themeKurenai() ? 1 : 0;
  if (s_wfTheme != curTheme) {
    for (int i = 0; i <= 110; i++) {
      float x = constrain((i - 10) / 70.0f, 0.0f, 1.0f);
      s_wfLut[i] = x < 0.08f ? C_BG() : heatV(x);
    }
    s_wfTheme = curTheme;
  }
  return s_wfLut[constrain(v, 0, 110)];
}

// ============================================================
//  SPECTRUM — ±2 MHz sweep and waterfall
// ============================================================
void drawSpectrum() {
  int W = spr.width();
  char buf[40];
  spr.fillSprite(C_BG());
  int mi = 0;
  for (int i = 1; i < SP_N; i++) if (g_spCur[i] > g_spCur[mi]) mi = i;
  snprintf(buf, sizeof(buf), L("peak %.2f  ·  %d dBm", "пик %.2f  ·  %d дБм"), spFreq(mi), g_spCur[mi]);
  char title[48]; snprintf(title, sizeof(title), L("Spectrum %s", "Спектр %s"), BANDS[g_band].name);
  uiHeader(title, buf, g_spCur[mi] > -60);

  int pl = UI_M + 4, pr = W - UI_M - 4, pt = UI_TOP, pb = UI_TOP + 54;
  auto px = [&](int i) { return pl + i * (pr - pl) / (SP_N - 1); };
  auto py = [&](int db) { int c = constrain(db, -110, -40); return pb - (c + 110) * (pb - pt) / 70; };
  uiFrame(pl, pt, pr - pl + 1, pb - pt + 1, UI_LINE());
  for (int db = -100; db <= -50; db += 10)
    for (int x = pl + 4; x < pr - 3; x += 4) spr.drawPixel(x, py(db), UI_LINE());
  // peaks dim, current level bright and "heats up" with strength
  for (int i = 1; i < SP_N; i++) spr.drawLine(px(i - 1), py(g_spPeak[i - 1]), px(i), py(g_spPeak[i]), UI_DIM());
  for (int i = 0; i < SP_N; i++) {
    int x = px(i), y = py(g_spCur[i]);
    spr.drawFastVLine(x, y, pb - y, scale565(heat(g_spCur[i] + 10), 25));
  }
  for (int i = 1; i < SP_N; i++)
    spr.drawLine(px(i - 1), py(g_spCur[i - 1]), px(i), py(g_spCur[i]), heat(max(g_spCur[i - 1], g_spCur[i]) + 10));
  // max marker
  int mx = px(mi), my = py(g_spCur[mi]);
  for (int y = pt + 2; y < pb; y += 3) spr.drawPixel(mx, y, C_ACC());
  spr.fillTriangle(mx, my - 3, mx - 4, my - 9, mx + 4, my - 9, C_ACC());

  // waterfall: new rows on top
  int wt = pb + 4, wh = UI_BOTTOM - wt - 8;
  for (int r = 0; r < g_wfCount && r < wh; r++) {
    int row = (g_wfHead - 1 - r + WF_ROWS) % WF_ROWS;
    for (int i = 0; i < SP_N; i++) {
      int x0 = px(i), x1 = (i + 1 < SP_N) ? px(i + 1) : pr + 1;
      if (g_wf) spr.drawFastHLine(x0, wt + r, x1 - x0, wfColor(g_wf[row][i]));
    }
  }
  // frequency axis
  spr.setTextColor(UI_DIM());
  snprintf(buf, sizeof(buf), "%.2f", BANDS[g_band].lo);
  spr.setTextDatum(TL_DATUM); txt(buf, pl, UI_BOTTOM - 6, 1);
  snprintf(buf, sizeof(buf), L("%.2f MHZ", "%.2f МГЦ"), (BANDS[g_band].lo + BANDS[g_band].hi) / 2);
  spr.setTextDatum(TC_DATUM); txt(buf, W / 2, UI_BOTTOM - 6, 1);
  snprintf(buf, sizeof(buf), "%.2f", BANDS[g_band].hi);
  spr.setTextDatum(TR_DATUM); txt(buf, pr, UI_BOTTOM - 6, 1);
  uiFooter(L("wheel: screens  ·  click: band", "колесо: экраны · нажать: полоса"), L("side: back", "кнопка: назад"));
  present();
}

// ============================================================
//  MONITOR — one frequency, catching bursts
// ============================================================
void drawMonitor(uint32_t now) {
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  int cur = g_monN ? g_mon[g_monN - 1] : -120;
  snprintf(buf, sizeof(buf), L("%d dBm  ·  %s", "%d дБм  ·  %s"), cur, g_burst ? L("signal!", "сигнал!") : L("quiet", "тихо"));
  char title[48]; snprintf(title, sizeof(title), L("Monitor %.2f", "Монитор %.2f"), BANDS[g_band].mon);
  uiHeader(title, buf, g_burst);

  // signal strip (~6 s), noise line and trigger threshold
  int gx = UI_M + 4, gy = UI_TOP, gw = W - 2 * UI_M - 8, gh = 62;
  uiFrame(gx, gy, gw, gh, UI_LINE());
  auto yy = [&](int db) { int c = constrain(db, -115, -35); return gy + gh - 3 - (c + 115) * (gh - 6) / 80; };
  for (int x = gx + 4; x < gx + gw - 4; x += 4) spr.drawPixel(x, yy((int)g_floor + BURST_ON_DB), UI_DIM());
  for (int i = 0; i < g_monN; i++) {
    int x = gx + 2 + (gw - 5) * (i + (MON_N - g_monN)) / (MON_N - 1);
    int y = yy(g_mon[i]);
    bool hi = g_mon[i] > g_floor + BURST_ON_DB;
    spr.drawFastVLine(x, y, gy + gh - 3 - y, hi ? heat(g_mon[i] + 10) : UI_LINE());
  }
  snprintf(buf, sizeof(buf), L("NOISE %d DBM   - - TRIGGER", "ШУМ %d ДБМ   - - ПОРОГ"), (int)g_floor);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_DIM());
  txt(buf, gx + 5, gy + 3, 1);

  // burst log
  int ly = gy + gh + 6;
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)g_events);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_HOT());
  txt(buf, UI_M + 8, ly, 6);
  spr.setTextColor(UI_DIM());
  txt(L("BURSTS", "ВСПЛЕСКИ"), UI_M + 8, ly + 46, 1);
  int tx = 96;
  spr.setTextColor(UI_DIM());
  txt(L("WHEN", "КОГДА"), tx, ly, 1); txt(L("LENGTH", "ДЛИНА"), tx + 70, ly, 1); txt(L("PEAK", "ПИК"), tx + 130, ly, 1);
  for (int i = 0; i < g_evN && i < 4; i++) {
    const Burst& b = g_ev[g_evN - 1 - i];
    uint32_t ago = (now - b.t) / 1000;
    if (ago < 60)        snprintf(buf, sizeof(buf), L("%lus ago", "%lu с наз."), (unsigned long)ago);
    else if (ago < 3600) snprintf(buf, sizeof(buf), L("%lum ago", "%lu мин наз."), (unsigned long)(ago / 60));
    else                 snprintf(buf, sizeof(buf), L("%luh ago", "%lu ч наз."), (unsigned long)(ago / 3600));
    int y = ly + 11 + i * 12;
    spr.setTextColor(i == 0 ? C_HOT() : UI_LABEL());
    txt(buf, tx, y, 1);
    snprintf(buf, sizeof(buf), L("%.1f s", "%.1f с"), b.durMs / 1000.0f); txt(buf, tx + 70, y, 1);
    snprintf(buf, sizeof(buf), "%d", b.peak); txt(buf, tx + 130, y, 1);
    int len = constrain((b.peak + 110) * 1, 2, 60);
    spr.fillRect(W - UI_M - 8 - len, y + 1, len, 5, heat(b.peak + 10));
  }
  if (g_evN == 0) {
    spr.setTextColor(UI_DIM());
    txt(L("PRESS A CAR KEY OR GATE REMOTE NEARBY", "НАЖМИТЕ РЯДОМ БРЕЛОК ИЛИ ПУЛЬТ ВОРОТ"), tx, ly + 14, 1);
  }
  uiFooter(L("wheel: screens  ·  click: band", "колесо: экраны · нажать: полоса"), L("side: back", "кнопка: назад"));
  present();
}

// ============================================================
void drawCcMissing() {
  char buf[40];
  spr.fillSprite(C_BG());
  uiHeader("Sub-GHz", L("radio not found", "радио не найдено"), true);
  snprintf(buf, sizeof(buf), L("chip answered 0x%02X", "ответ чипа 0x%02X"), g_ccVersion);
  uiEmpty(L("CC1101 not found", "CC1101 не найден"), buf);
  uiFooter(L("wheel: screens", "колесо: экраны"), L("side: back", "кнопка: назад"));
  present();
}

// ============================================================
//  SENSORS — who transmits on the frequency: outdoor thermometers, bells, remotes
//  table on top, shape of the last packet (analyzer) below
// ============================================================
static void agoStr(char* b, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) snprintf(b, n, L("%lus", "%luс"), (unsigned long)s);
  else if (s < 3600) snprintf(b, n, L("%lum", "%luм"), (unsigned long)(s / 60));
  else snprintf(b, n, L("%luh", "%luч"), (unsigned long)(s / 3600));
}

// short protocol name for the table
static void shortName(const SensRec& r, char* b, size_t n) {
  const char* p = r.proto;
  if (!strncmp(p, "LaCrosse-", 9)) p = "LaCrosse";
  else if (!strncmp(p, "FineOffset-", 11)) p = "FineOffset";
  else if (!strncmp(p, "Acurite-", 8)) p = "Acurite";
  if (r.kind == OOK_REMOTE) snprintf(b, n, "%s %04lX", strcmp(r.proto, "Rolling") ? L("BELL/REMOTE", "ЗВОНОК/ПУЛЬТ") : L("ROLLING", "ПЛАВАЮЩИЙ"), (unsigned long)(r.id & 0xFFFF));
  else if (r.ch) snprintf(b, n, L("%s CH%d", "%s К%d"), p, r.ch);
  else snprintf(b, n, "%s %02lX", p, (unsigned long)(r.id & 0xFF));
  utf8UpperInPlace(b);
}

static void degree(int x, int y, uint16_t col) { spr.drawCircle(x, y, 2, col); }

static void drawPulseTrain(int x0, int y0, int w, int h) {
  const SensLast& L = g_sensLast;
  uiFrame(x0, y0, w, h, UI_LINE());
  char buf[64];
  spr.setTextDatum(TL_DATUM);
  if (!L.valid) {
    spr.setTextColor(UI_DIM());
    txt(L("NO SIGNAL YET", "СИГНАЛОВ ПОКА НЕТ"), x0 + 6, y0 + 4, 1);
    for (int x = x0 + 6; x < x0 + w - 6; x += 3) spr.drawPixel(x, y0 + h - 8, UI_LINE());
    return;
  }
  const OokInfo& I = L.info;
  bool sensor = strcmp(I.cls, "sensor") == 0;
  // caption: modulation, bits, durations; sensor: protocol, remote: fingerprint only
  if (sensor) snprintf(buf, sizeof buf, "%s %db  P%u/%u G%u/%u", I.mod, I.bits, I.shortW, I.longW, I.shortG, I.longG ? I.longG : I.syncG);
  else snprintf(buf, sizeof buf, "%s %db  P%u/%u  #%04X", I.mod, I.bits, I.shortW, I.longW, I.hash);
  spr.setTextColor(UI_LABEL());
  txt(buf, x0 + 6, y0 + 4, 1);
  char cls[24];
  snprintf(cls, sizeof cls, "%s", sensor ? I.proto : I.cls);
  utf8UpperInPlace(cls);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(sensor ? C_ACC() : C_HOT());
  txt(cls, x0 + w - 6, y0 + 4, 1);
  // the packet itself: square wave, carrier on top, silence at the bottom
  uint32_t tot = 0;
  for (int i = 0; i < L.ptN * 2; i++) tot += L.pt[i];
  if (!tot) return;
  int px0 = x0 + 6, pw = w - 12 - (sensor ? 0 : 18);
  int hiY = y0 + 16, loY = y0 + h - 5;
  uint32_t acc = 0; int prevX = px0, prevY = loY;
  uint16_t col = sensor ? C_ACC() : heat(L.rssi + 10);
  for (int i = 0; i < L.ptN * 2; i++) {
    acc += L.pt[i];
    int x = px0 + (int)((uint64_t)acc * pw / tot);
    int y = (i & 1) ? loY : hiY;
    spr.drawFastVLine(prevX, min(prevY, y), abs(y - prevY) + 1, col);
    spr.drawFastHLine(prevX, y, x - prevX + 1, col);
    if (!(i & 1)) for (int yy = hiY + 2; yy < loY; yy += 2) spr.drawFastHLine(prevX, yy, max(1, x - prevX), scale565(col, 18));
    prevX = x; prevY = y;
  }
  if (!sensor) {                                 // no more drawing: a remote can't be replayed from the picture
    spr.setTextColor(UI_DIM()); spr.setTextDatum(TL_DATUM);
    txt("...", px0 + pw + 3, loY - 7, 1);
  }
}

void drawSensors(uint32_t now) {
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  char title[48]; snprintf(title, sizeof(title), L("Sensors %.2f", "Датчики %.2f"), BANDS[g_band].mon);
  bool fresh = g_sensLast.valid && msSince(now, g_sensLast.ms) < 1500;
  if (g_sensN) snprintf(buf, sizeof buf, L("%d heard%s", "услышано %d%s"), g_sensN, fresh ? L("  · rx", "  · приём") : "");
  else snprintf(buf, sizeof buf, "%s", fresh ? L("rx", "приём") : L("listening", "слушаю"));
  uiHeader(title, buf, fresh);

  uint8_t idx[SENS_MAX];
  int n = sensSorted(idx, SENS_MAX, now);
  const int rowH = 16, top = UI_TOP + 1, rows = 5;
  if (!n) {
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(UI_LABEL());
    txt(L("LISTENING...", "СЛУШАЮ..."), W / 2, top + 26, 4);
    spr.setTextColor(UI_DIM());
    txt(L("OUTDOOR THERMOMETERS, DOORBELLS, REMOTES", "УЛИЧНЫЕ ТЕРМОМЕТРЫ, ЗВОНКИ, ПУЛЬТЫ"), W / 2, top + 52, 1);
    snprintf(buf, sizeof buf, L("%lu PACKETS  ·  %lu NOISE", "%lu ПОСЫЛОК  ·  %lu ШУМ"), (unsigned long)g_sensPackets, (unsigned long)g_sensNoise);
    txt(buf, W / 2, top + 66, 1);
  }
  int shown = n > rows ? rows - 1 : n;             // didn't fit: the last row is "+N"
  for (int k = 0; k < shown; k++) {
    const SensRec& r = g_sens[idx[k]];
    int y = top + k * rowH;
    bool old = msSince(now, r.lastMs) > 10 * 60 * 1000UL;
    bool rx = msSince(now, r.lastMs) < 1500;
    if (rx) uiRowHighlight(y, rowH - 1);
    spr.setTextDatum(TL_DATUM);
    shortName(r, buf, sizeof buf);
    spr.setTextColor(old ? UI_DIM() : UI_LABEL());
    txt(buf, UI_M + 4, y + 4, 1);
    // temperature / button
    spr.setTextDatum(TR_DATUM);
    if (r.kind == OOK_REMOTE) {
      spr.setTextColor(old ? UI_DIM() : C_HOT());
      if (r.btn) snprintf(buf, sizeof buf, L("BTN %d", "КН %d"), r.btn); else strcpy(buf, L("PRESS", "НАЖАТИЕ"));
      txt(buf, 172, y + 4, 1);
    } else {
      snprintf(buf, sizeof buf, "%.1f", r.t);
      uint16_t c = old ? UI_DIM() : C_HOT();
      spr.setTextColor(c);
      txt(buf, 160, y, 2);
      degree(164, y + 3, c);
      spr.setTextDatum(TL_DATUM); txt("C", 168, y + 4, 1);
      if (r.h >= 0) { snprintf(buf, sizeof buf, "%d%%", r.h); spr.setTextDatum(TR_DATUM); spr.setTextColor(old ? UI_DIM() : C_ACC()); txt(buf, 212, y, 2); }
    }
    // battery: outline, fill inside (low = alert color)
    if (r.bat >= 0) {
      int bx = 220, by = y + 4;
      uint16_t bc = r.bat ? UI_DIM() : C_ALERT();
      spr.drawRect(bx, by, 11, 7, bc); spr.drawFastVLine(bx + 11, by + 2, 3, bc);
      spr.fillRect(bx + 2, by + 2, r.bat ? 7 : 2, 3, bc);
    }
    // signal strength bar
    int len = constrain((r.rssi + 110) * 30 / 70, 1, 30);
    spr.fillRect(238, y + 5, 30, 5, UI_LINE());
    spr.fillRect(238, y + 5, len, 5, old ? UI_DIM() : heat(r.rssi + 10));
    agoStr(buf, sizeof buf, msSince(now, r.lastMs));
    spr.setTextDatum(TR_DATUM); spr.setTextColor(rx ? C_ACC() : UI_DIM());
    txt(buf, W - UI_M - 2, y + 4, 1);
  }
  if (n > rows) {
    snprintf(buf, sizeof buf, L("+%d MORE IN THE APP", "+%d ЕЩЁ В ПРИЛОЖЕНИИ"), n - shown);
    spr.setTextDatum(TL_DATUM); spr.setTextColor(UI_DIM());
    txt(buf, UI_M + 4, top + shown * rowH + 4, 1);
  }
  drawPulseTrain(UI_M, 108, W - 2 * UI_M, UI_BOTTOM - 108);
  uiFooter(L("wheel: screens  ·  click: band", "колесо: экраны · нажать: полоса"), L("side: back", "кнопка: назад"));
  present();
}
