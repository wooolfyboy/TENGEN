// ============================================================
//  pc_screens.cpp — screens of the PC section
//
//  OVERVIEW — big CPU + one-minute graph; RAM, disk, network, battery on the right
//  CORES    — per-core load bars
//  MUSIC    — wheel sets laptop volume, click = pause/play
//  WAITING  — if the laptop sends nothing: step-by-step setup
// ============================================================
#include "core/timeutil.h"
#include "core/lang.h"
#include "pc/pc.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"

static uint16_t loadCol(float pct) {                 // load: cold -> hot, > 90% = alert
  return pct >= 90 ? C_ALERT() : heatV(0.25f + 0.75f * pct / 100.0f);
}
static void rateStr(float kb, char* out, size_t n) {
  if (kb >= 1000) snprintf(out, n, L("%.1f MB/s", "%.1f МБ/с"), kb / 1024.0f);
  else            snprintf(out, n, L("%d KB/s", "%d КБ/с"), (int)kb);
}
static void upStr(uint32_t s, char* out, size_t n) {
  if (s >= 86400) snprintf(out, n, L("up %lud %luh", "работает %lu д %lu ч"), (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24));
  else            snprintf(out, n, L("up %luh %lum", "работает %lu ч %lu мин"), (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
}
static void bar(int x, int y, int w, int h, float pct, uint16_t col) {
  spr.drawRect(x, y, w, h, UI_LINE());
  int f = (int)((w - 2) * constrain(pct, 0.0f, 100.0f) / 100.0f);
  if (f > 0) spr.fillRect(x + 1, y + 1, f, h - 2, col);
}
void drawPc(uint32_t now) {
  const PcStat& p = g_pc;
  const int W = spr.width();
  char buf[40], a[16], b[16];
  spr.fillSprite(C_BG());
  uint32_t age = msSince(now, p.t) / 1000;
  char hdr[32];
  if (age >= 3) snprintf(hdr, sizeof(hdr), L("no data %lus", "нет данных %lu с"), (unsigned long)age);   // short pause: numbers are stale
  uiHeader(L("PC", "ПК"), age >= 3 ? hdr : (p.host[0] ? p.host : L("computer", "компьютер")), p.cpu >= 90);

  // ---- left: CPU ----
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_LABEL());
  txt("CPU", UI_M + 6, UI_TOP + 4, 1);
  if (p.freq > 0) { snprintf(buf, sizeof(buf), L("%.1f GHZ", "%.1f ГГЦ"), p.freq); spr.setTextColor(UI_DIM()); txt(buf, UI_M + 34, UI_TOP + 4, 1); }
  snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(p.cpu));
  spr.setTextColor(loadCol(p.cpu));
  txt(buf, UI_M + 4, UI_TOP + 14, 6);
  // graph: CPU (bright) and RAM (dim) over a minute
  const int gx = UI_M + 4, gy = UI_TOP + 70, gw = 140, gh = 54;
  uiFrame(gx, gy, gw, gh, UI_LINE());
  for (int k = 1; k < 4; k++) spr.drawFastHLine(gx + 1, gy + gh * k / 4, gw - 2, UI_LINE());
  auto plot = [&](const float* h, uint16_t col, bool fill) {
    int px = -1, py = 0;
    for (int i = 0; i < p.histN; i++) {
      int x = gx + gw - 2 - (p.histN - 1 - i) * (gw - 3) / (PC_HIST - 1);
      int y = gy + gh - 2 - (int)((gh - 4) * constrain(h[i], 0.0f, 100.0f) / 100.0f);
      if (fill) spr.drawFastVLine(x, y, gy + gh - 1 - y, TC(10, 45, 56));
      if (px >= 0) spr.drawLine(px, py, x, y, col);
      px = x; py = y;
    }
  };
  plot(p.ramH, UI_DIM(), false);
  plot(p.cpuH, C_ACC(), true);
  plot(p.cpuH, C_ACC(), false);
  spr.setTextColor(UI_DIM());
  txt("60 S", gx + 3, gy + 3, 1);

  // ---- right: RAM, disk, network, battery ----
  const int rx = 164, rw = W - rx - UI_M - 4;
  int y = UI_TOP + 4;
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_LABEL()); txt("RAM", rx, y, 1);
  snprintf(buf, sizeof(buf), L("%.1f / %.0f GB", "%.1f / %.0f ГБ"), p.ramU, p.ramT);
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_HOT()); txt(buf, rx + rw, y, 1);
  bar(rx, y + 11, rw, 8, p.ram, loadCol(p.ram));

  y += 28;
  spr.setTextDatum(TL_DATUM); spr.setTextColor(UI_LABEL()); txt(L("DISK C:", "ДИСК C:"), rx, y, 1);
  snprintf(buf, sizeof(buf), L("%d%% FULL", "ЗАНЯТО %d%%"), (int)p.disk);
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_HOT()); txt(buf, rx + rw, y, 1);
  bar(rx, y + 11, rw, 8, p.disk, p.disk >= 90 ? C_ALERT() : UI_LABEL());
  snprintf(buf, sizeof(buf), L("R %.1f  W %.1f MB/S", "Ч %.1f  З %.1f МБ/С"), p.dR, p.dW);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(UI_DIM()); txt(buf, rx, y + 22, 1);

  y += 38;
  spr.setTextColor(UI_LABEL()); txt(L("NET", "СЕТЬ"), rx, y, 1);
  rateStr(p.nD, a, sizeof(a)); rateStr(p.nU, b, sizeof(b));
  spr.setTextColor(C_HOT());
  snprintf(buf, sizeof(buf), L("DN %s", "ВХ %s"), a); txt(buf, rx, y + 11, 1);
  snprintf(buf, sizeof(buf), L("UP %s", "ИСХ %s"), b); txt(buf, rx, y + 22, 1);

  y += 36;
  if (p.bat >= 0) {
    spr.setTextColor(UI_LABEL()); txt(L("BATTERY", "БАТАРЕЯ"), rx, y, 1);
    snprintf(buf, sizeof(buf), "%d%% %s", p.bat, p.plug ? L("PLUGGED", "ОТ СЕТИ") : "");
    spr.setTextDatum(TR_DATUM); spr.setTextColor(p.bat <= 15 && !p.plug ? C_ALERT() : C_HOT()); txt(buf, rx + rw, y, 1);
  }

  char top[48]; snprintf(top, sizeof(top), L("top: %s", "больше всех: %s"), p.top[0] ? p.top : "-");
  upStr(p.up, buf, sizeof(buf));
  uiFooter(top, buf);
  present();
}

void drawPcCores(uint32_t now) {
  const PcStat& p = g_pc;
  const int W = spr.width();
  char buf[40];
  spr.fillSprite(C_BG());
  snprintf(buf, sizeof(buf), L("%d cores  ·  %d%%", "%d ядер  ·  %d%%"), p.nCores, (int)lroundf(p.cpu));
  uiHeader(L("Cores", "Ядра"), buf, p.cpu >= 90);
  int n = max(1, p.nCores);
  const int x0 = UI_M + 6, x1 = W - UI_M - 6, top = UI_TOP + 8, bot = UI_BOTTOM - 14;
  int slot = (x1 - x0) / n, bw = max(4, slot - (n > 8 ? 3 : 6));
  for (int k = 25; k < 100; k += 25) {
    int yy = bot - (bot - top) * k / 100;
    for (int x = x0; x < x1; x += 4) spr.drawPixel(x, yy, UI_LINE());
  }
  spr.setTextDatum(TC_DATUM);
  for (int i = 0; i < p.nCores; i++) {
    int x = x0 + i * slot + (slot - bw) / 2;
    int h = (bot - top) * p.core[i] / 100;
    spr.drawRect(x, top, bw, bot - top, UI_LINE());
    if (h > 0) spr.fillRect(x + 1, bot - h, bw - 2, h, loadCol(p.core[i]));
    spr.setTextColor(UI_DIM());
    snprintf(buf, sizeof(buf), "%d", i + 1);
    txt(buf, x + bw / 2, bot + 3, 1);
  }
  uiFooter(L("wheel: screens", "колесо: экраны"), L("side: back", "кнопка: назад"));
  present();
  (void)now;
}

void drawPcWaiting(uint32_t now) {
  const int W = spr.width();
  spr.fillSprite(C_BG());
  bool lost = g_pc.have;
  uiHeader(L("PC", "ПК"), lost ? L("computer stopped", "компьютер отключился") : L("waiting for computer", "жду компьютер"), lost);
  spr.setTextDatum(TL_DATUM);
  int x = UI_M + 10, y = UI_TOP + 10;
  spr.setTextColor(C_ACC());  txt("1", x, y, 4);
  spr.setTextColor(C_HOT());  txt(L("CONNECT TENGEN TO THE COMPUTER (USB)", "ПОДКЛЮЧИТЕ TENGEN К КОМПЬЮТЕРУ (USB)"), x + 22, y + 6, 1);
  y += 34;
  spr.setTextColor(C_ACC());  txt("2", x, y, 4);
  spr.setTextColor(C_HOT());  txt(L("ON THE COMPUTER START", "НА КОМПЬЮТЕРЕ ЗАПУСТИТЕ"), x + 22, y, 1);
  spr.setTextColor(C_ACC());  txt("TENGEN Desktop", x + 22, y + 13, 1);
  y += 34;
  spr.setTextColor(C_ACC());  txt("3", x, y, 4);
  spr.setTextColor(C_HOT());  txt(L("WAIT A FEW SECONDS · DATA APPEARS HERE", "ПОДОЖДИТЕ - ДАННЫЕ ПОЯВЯТСЯ ЗДЕСЬ"), x + 22, y + 6, 1);
  // "waiting pulse"
  int k = (now / 300) % 4;
  for (int i = 0; i < 3; i++) spr.fillRect(W - UI_M - 30 + i * 8, UI_TOP + 112, 5, 5, i < k ? C_ACC() : UI_LINE());
  uiFooter("", L("side: back", "кнопка: назад"));
  present();
}

// ---------- MUSIC: now playing + remote ----------
static void mmss(int s, char* out, size_t n) {
  if (s < 0) { snprintf(out, n, "--:--"); return; }
  if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
  else           snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

// long title -> marquee: a window of UTF-8 characters that fits the width
static void marquee(const char* s, int x, int y, int w, int font, uint32_t now) {
  if (txtW(s, font) <= w) { txt(s, x, y, font); return; }
  int starts[96], nc = 0, len = strlen(s);
  for (int k = 0; k < len && nc < 96; k++) if (((uint8_t)s[k] & 0xC0) != 0x80) starts[nc++] = k;
  int maxStart = nc - 1;                              // first start from which the tail fits
  for (int c = 0; c < nc; c++) if (txtW(s + starts[c], font) <= w) { maxStart = c; break; }
  int cycle = maxStart + 8;                           // +8 pause steps (at start and end)
  int step = (now / 260) % cycle;
  int c0 = constrain(step - 4, 0, maxStart);
  char buf[100];
  int m = 0;
  for (int c = c0; c < nc; c++) {                     // add whole characters while they fit
    int a = starts[c], b = c + 1 < nc ? starts[c + 1] : len;
    if (m + (b - a) >= (int)sizeof(buf)) break;
    memcpy(buf + m, s + a, b - a); buf[m + (b - a)] = 0;
    if (txtW(buf, font) > w) { buf[m] = 0; break; }
    m += b - a;
  }
  txt(buf, x, y, font);
}

void drawPcMedia(uint32_t now) {
  const PcStat& p = g_pc;
  const int W = spr.width();
  char buf[48], a[16], b[16];
  spr.fillSprite(C_BG());
  bool trk = p.trk && msSince(now, p.trkT) < 10000;
  char app[40]; snprintf(app, sizeof(app), "%s", trk && p.app[0] ? p.app : (p.host[0] ? p.host : L("computer", "компьютер")));
  uiHeader(L("Media", "Музыка"), app);

  // ---- right: volume as a thin bar (brighter while the wheel turns) ----
  const int vx = W - UI_M - 16, vy = UI_TOP + 6, vh = 92, vw = 10;
  bool known = p.vol >= 0 && msSince(now, p.volT) < 15000;
  uint32_t since = msSince(now, g_pcLastCmdMs);
  bool turning = since < 700 && (g_pcLastCmd == 1 || g_pcLastCmd == -1);
  spr.drawRect(vx, vy, vw, vh, turning ? C_ACC() : UI_LINE());
  if (known) {
    int h = (vh - 2) * p.vol / 100;
    spr.fillRect(vx + 1, vy + vh - 1 - h, vw - 2, h, p.mute ? UI_DIM() : (turning ? C_HOT() : C_ACC()));
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(turning ? C_HOT() : UI_LABEL());
  if (known) snprintf(buf, sizeof(buf), p.mute ? L("MUTE", "БЕЗ ЗВ.") : "%d", p.vol); else snprintf(buf, sizeof(buf), "%s", L("VOL", "ГРОМК"));
  txt(buf, vx + vw / 2, vy + vh + 4, 1);

  const int tx = UI_M + 6, tw = vx - tx - 12;
  if (!trk) {
    // ---- no track info: spinning vinyl record, big volume beside it ----
    g_fastFrame = true;
    static float rot = 0;
    static uint32_t lastMs = 0;
    float dt = lastMs ? min<uint32_t>(msSince(now, lastMs), 100) * 0.001f : 0.03f;
    lastMs = now;
    rot += dt * (turning ? 7.0f : 1.8f);                   // wheel turning: the record speeds up
    const int cx = tx + 66, cy = UI_TOP + 64, R = 58;
    spr.fillCircle(cx, cy, R, TC(6, 18, 22));
    spr.drawCircle(cx, cy, R, UI_LINE());
    for (int r = 24; r < R - 2; r += 4) spr.drawCircle(cx, cy, r, TC(12, 34, 41));   // grooves
    // highlights: two symmetric light sectors on the grooves, spinning with the record
    for (int k = -6; k <= 6; k++) {
      float ang = rot + k * 0.035f, ca = cosf(ang), sa = sinf(ang);
      int br = 110 - abs(k) * 15;
      uint16_t hc = TC(br / 3, br, br + 20 > 255 ? 255 : br + 20);
      for (int r = 26; r < R - 3; r += 4) {
        int dx = (int)(ca * r), dy = (int)(sa * r);
        spr.drawPixel(cx + dx, cy + dy, hc);
        spr.drawPixel(cx - dx, cy - dy, hc);
      }
    }
    // record label, spinning
    spr.fillCircle(cx, cy, 19, TC(10, 60, 74));
    spr.drawCircle(cx, cy, 19, C_ACC());
    float la = rot * 1.0f;
    spr.drawLine(cx + (int)(cosf(la) * 8), cy + (int)(sinf(la) * 8), cx + (int)(cosf(la) * 16), cy + (int)(sinf(la) * 16), C_HOT());
    spr.fillCircle(cx, cy, 3, C_HOT());
    // tonearm
    const int px = cx + R + 12, py = cy - R + 6;
    spr.fillCircle(px, py, 6, UI_LINE());
    spr.drawCircle(px, py, 6, UI_LABEL());
    int nx = cx + 30, ny = cy - 22;
    spr.drawLine(px, py, nx + 6, ny - 10, UI_LABEL());
    spr.drawLine(px + 1, py, nx + 7, ny - 10, UI_LABEL());
    spr.fillRect(nx, ny - 12, 9, 6, C_ACC());
    // right of the record: big volume
    const int gx = px + 16;
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(UI_LABEL());
    txt(p.mute ? L("MUTED", "БЕЗ ЗВУКА") : L("VOLUME", "ГРОМКОСТЬ"), gx, UI_TOP + 22, 1);
    spr.setTextColor(p.mute ? UI_DIM() : (turning ? C_HOT() : C_ACC()));
    if (known) { snprintf(buf, sizeof(buf), "%d", p.vol); txt(buf, gx, UI_TOP + 32, 6); }
    else txt("--", gx, UI_TOP + 32, 6);
    spr.setTextColor(UI_DIM());
    txt(L("NO TRACK INFO", "НЕТ ДАННЫХ О ТРЕКЕ"), gx, UI_TOP + 84, 1);
    txt(L("WHEEL: VOLUME", "КОЛЕСО: ГРОМКОСТЬ"), gx, UI_TOP + 96, 1);
  } else {
    // ---- title and artist ----
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_HOT());
    marquee(p.title[0] ? p.title : L("(no title)", "(без названия)"), tx, UI_TOP + 10, tw, 5, now);   // fonts with Cyrillic
    spr.setTextColor(UI_LABEL());
    char art[96];
    fitText(p.artist, art, sizeof(art), tw, 3);
    txt(art, tx, UI_TOP + 40, 3);
    // ---- progress: position between updates is extrapolated ----
    int pos = p.pos;
    if (pos >= 0 && p.playing) pos += msSince(now, p.trkT) / 1000;
    if (p.dur > 0) pos = min(pos, p.dur);
    const int py = UI_TOP + 70;
    spr.drawFastHLine(tx, py + 3, tw, UI_LINE());
    if (p.dur > 0 && pos >= 0) {
      int f = tw * pos / p.dur;
      spr.fillRect(tx, py + 2, f, 3, C_ACC());
      spr.fillCircle(tx + f, py + 3, 4, C_HOT());
    }
    mmss(pos, a, sizeof(a)); mmss(p.dur, b, sizeof(b));
    spr.setTextColor(UI_DIM());
    txt(a, tx, py + 11, 1);
    spr.setTextDatum(TR_DATUM);
    txt(b, tx + tw, py + 11, 1);
    // ---- state: playing / paused (and a flash after a click) ----
    bool flash = g_pcLastCmd == 2 && since < 500;
    uint16_t ic = flash ? C_HOT() : C_ACC();
    int ix = tx, iy = UI_TOP + 98;
    if (p.playing) spr.fillTriangle(ix, iy, ix, iy + 16, ix + 14, iy + 8, ic);
    else { spr.fillRect(ix, iy, 5, 16, ic); spr.fillRect(ix + 9, iy, 5, 16, ic); }
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(UI_LABEL());
    txt(p.playing ? L("PLAYING", "ИГРАЕТ") : L("PAUSED", "ПАУЗА"), ix + 22, iy + 4, 1);
  }
  uiFooter(L("wheel: volume  ·  click: play/pause", "колесо: громкость · нажать: пауза"), L("side: back", "кнопка: назад"));
  present();
}
