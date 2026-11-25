// ============================================================
//  wifi_screens.cpp — Wi-Fi section screens: NETWORKS (+details), CHANNELS, ACTIVITY, CLIENTS, FIND.
//  All built from core/ui.h components on a common grid.
// ============================================================
#include <esp_wifi.h>
#include "core/lang.h"
#include "core/timeutil.h"
#include "core/settings.h"
#include "wifi/wifi_model.h"
#include "core/board.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/leds.h"
#include "audio/audio.h"

// security glyph; open and WEP are inverted, as an alert
static char secGlyph(uint8_t e) {
  switch (e) {
    case WIFI_AUTH_OPEN:            return 'O';
    case WIFI_AUTH_WEP:             return '!';
    case WIFI_AUTH_WPA_PSK:         return '1';
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:    return '2';
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_OWE:             return '3';
    case WIFI_AUTH_WPA2_ENTERPRISE: return 'E';
    default:                        return '?';
  }
}
static bool secAlert(uint8_t e) { return e == WIFI_AUTH_OPEN || e == WIFI_AUTH_WEP; }

static void upperCopy(const char* s, char* out, size_t n) { utf8Upper(s, out, n); }

// "scanning" blinking dot at the end of the header line
static void scanDot() {
  if ((g_scanning || g_prom) && (millis() / 300) % 2) spr.fillRect(spr.width() - UI_M - 3, 18, 3, 3, C_ACC());
}

static bool waiting() {
  if (!g_aps.empty()) return false;
  uiEmpty(g_wEverScanned ? L("no networks", "сетей нет") : L("scanning", "поиск"), L("listening for access points, 2.4 GHz only", "ищу точки доступа, только 2,4 ГГц"));
  return true;
}

// ============================================================
//  NETWORKS — list and details
// ============================================================
void drawNets(uint32_t now) {
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  snprintf(buf, sizeof(buf), L("%d  ·  strongest first", "%d  ·  сильные сверху"), (int)g_wOrder.size());
  uiHeader(L("Networks", "Сети"), buf);
  scanDot();
  if (g_wOrder.empty()) { waiting(); uiFooter("", L("side: back", "кнопка: назад")); present(); return; }

  for (int i = 0; i < LIST_ROWS; i++) {
    int idx = g_wTop + i;
    if (idx >= (int)g_wOrder.size()) break;
    uint64_t k = g_wOrder[idx];
    auto it = g_aps.find(k);
    if (it == g_aps.end()) continue;
    const Ap& a = it->second;
    int y = LIST_Y0 + i * LIST_ROWH, cy = y + LIST_ROWH / 2 - 1;
    if (k == g_wSel) uiRowHighlight(y, LIST_ROWH - 1);
    bool alert = secAlert(a.enc);
    drawGlyph(UI_M + 8, cy, secGlyph(a.enc), alert ? C_ALERT() : UI_LABEL(), alert);
    fitText(ssidOr(a), buf, sizeof(buf), 136, 2);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(a.ssid[0] ? C_HOT() : UI_DIM());
    txt(buf, UI_M + 18, cy, 2);
    snprintf(buf, sizeof(buf), L("CH%d", "К%d"), a.ch);
    spr.setTextColor(UI_DIM());
    txt(buf, 166, cy, 1);
    const char* tag = alert ? L("OPEN", "ОТКР") : a.hotspot ? L("PHONE", "ТЕЛ") : a.gen == 6 ? "WI-FI6" : "";
    if (a.enc == WIFI_AUTH_WEP) tag = "WEP";
    spr.setTextColor(alert ? C_ALERT() : UI_LABEL());
    txt(tag, 196, cy, 1);
    bool stale = msSince(now, a.t) > 6000;
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(stale ? UI_DIM() : heat(a.rssi));
    txtNum(a.rssi, 266, cy, 2);
    drawBars(274, cy - 7, a.rssi, heat(a.rssi));
  }
  int n = (int)g_wOrder.size();
  if (n > LIST_ROWS) {
    int trackH = LIST_ROWS * LIST_ROWH;
    int barH = max(8, trackH * LIST_ROWS / n);
    int barY = LIST_Y0 + (trackH - barH) * g_wTop / (n - LIST_ROWS);
    spr.fillRect(W - UI_M + 2, LIST_Y0, 2, trackH, UI_LINE());
    spr.fillRect(W - UI_M + 2, barY, 2, barH, C_ACC());
  }
  uiFooter(L("wheel: pick  ·  click: details", "колесо: выбор · нажать: подробно"), L("side: back", "кнопка: назад"));
  present();
}

void drawNetDetail(uint32_t now) {
  int W = spr.width();
  char buf[96], tmp[48];
  auto it = g_aps.find(g_wSel);
  if (it == g_aps.end()) { g_wDetail = false; drawNets(now); return; }
  const Ap& a = it->second;
  spr.fillSprite(C_BG());
  int pos = -1;
  for (size_t i = 0; i < g_wOrder.size(); i++) if (g_wOrder[i] == g_wSel) pos = (int)i;
  if (pos >= 0) snprintf(buf, sizeof(buf), L("%d / %d  ·  click: list", "%d / %d · нажать: список"), pos + 1, (int)g_wOrder.size());
  else          snprintf(buf, sizeof(buf), "%s", L("click: list", "нажать: список"));
  uiHeader(L("Network", "Сеть"), buf);

  bool alert = secAlert(a.enc);
  drawGlyph(UI_M + 8, UI_TOP + 11, secGlyph(a.enc), alert ? C_ALERT() : UI_LABEL(), alert);
  fitText(ssidOr(a), tmp, sizeof(tmp), W - 2 * UI_M - 24, 4);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(a.ssid[0] ? C_HOT() : UI_DIM());
  txt(tmp, UI_M + 20, UI_TOP, 4);
  snprintf(buf, sizeof(buf), L("%s  CH %d  %d MHZ  %s", "%s  К %d  %d МГЦ  %s"), encName(a.enc), a.ch, a.sec ? 40 : 20, genName(a.gen));
  char bu[96]; upperCopy(buf, bu, sizeof(bu));
  spr.setTextColor(alert ? C_ALERT() : UI_LABEL());
  txt(bu, UI_M + 20, UI_TOP + 26, 1);

  // readouts
  int sameCh = 0, near = 0;
  for (auto& kv : g_aps) {
    if (kv.first == g_wSel) continue;
    float d = fabsf(apCenter(kv.second) - apCenter(a));
    if (d < 0.5f) sameCh++; else if (d < apHalf(kv.second) + apHalf(a)) near++;
  }
  uint32_t ago = msSince(now, a.t) / 1000;
  int x0 = UI_M + 8, y0 = UI_TOP + 40, cw = 64;
  snprintf(tmp, sizeof(tmp), "%d", a.rssi);
  uiReadout(x0, y0, ago >= 6 ? L("signal (old)", "сигнал (давно)") : L("signal", "сигнал"), tmp, heat(a.rssi));
  snprintf(tmp, sizeof(tmp), "%d/%d", a.rmin, a.rmax);
  uiReadout(x0 + cw, y0, L("range", "разброс"), tmp, C_HOT());
  uint32_t secs = msSince(now, a.first) / 1000;
  snprintf(tmp, sizeof(tmp), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  uiReadout(x0 + 2 * cw, y0, L("seen", "видна"), tmp, C_HOT());
  snprintf(tmp, sizeof(tmp), "%d+%d", sameCh, near);
  uiReadout(x0, y0 + 28, L("neighbours", "соседи"), tmp, sameCh + near > 3 ? C_ALERT() : C_HOT());
  int cl = clientsOf(g_wSel);
  if (cl > 0) snprintf(tmp, sizeof(tmp), "%d", cl); else snprintf(tmp, sizeof(tmp), "?");
  uiReadout(x0 + cw, y0 + 28, L("devices", "клиенты"), tmp, C_HOT());
  uiReadout(x0 + 2 * cw, y0 + 28, L("band", "полоса"), L("2.4G", "2,4Г"), C_HOT());

  int gx = 206, gy = y0, gw = W - UI_M - 4 - gx, gh = 52;
  uiFrame(gx, gy, gw, gh, UI_LINE());
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_DIM());
  txt(L("SIGNAL · 2 MIN", "СИГНАЛ · 2 МИН"), gx + 5, gy + 3, 1);
  drawSpark(gx, gy, gw, gh, g_wSpark, g_wSparkN, W_SPARK_N, heat(a.rssi));

  int y = UI_TOP + 98;
  const uint8_t* b = a.bssid;
  bool local = b[0] & 0x02;
  if (a.hotspot)     snprintf(buf, sizeof(buf), L("MAKER %s  ·  PHONE HOTSPOT", "ПРОИЗВ. %s  ·  ТОЧКА ТЕЛЕФОНА"), a.vendor ? a.vendor : "?");
  else if (a.vendor) snprintf(buf, sizeof(buf), L("MAKER %s", "ПРОИЗВ. %s"), a.vendor);
  else if (local)    snprintf(buf, sizeof(buf), "%s", L("MAKER HIDDEN (RANDOM MAC)", "ПРОИЗВОДИТЕЛЬ СКРЫТ (СЛУЧАЙНЫЙ MAC)"));
  else               snprintf(buf, sizeof(buf), "%s", L("MAKER UNKNOWN", "ПРОИЗВОДИТЕЛЬ НЕИЗВЕСТЕН"));
  upperCopy(buf, bu, sizeof(bu));
  spr.setTextColor(UI_LABEL());
  txt(bu, x0, y, 1); y += 10;
  snprintf(buf, sizeof(buf), "BSSID %02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
  spr.setTextColor(UI_DIM());
  txt(buf, x0, y, 1); y += 10;
  if (alert) {
    spr.setTextColor(C_ALERT());
    txt(a.enc == WIFI_AUTH_WEP ? L("! WEP IS BROKEN: TRAFFIC CAN BE READ", "! WEP ВЗЛОМАН: ТРАФИК МОЖНО ПРОЧИТАТЬ") : L("! OPEN NETWORK: TRAFFIC IS NOT ENCRYPTED", "! ОТКРЫТАЯ СЕТЬ: ТРАФИК НЕ ШИФРУЕТСЯ"), x0, y, 1);
  }
  present();
}

// ============================================================
//  CHANNELS — who occupies which channel
// ============================================================
void drawChannels(uint32_t now) {
  int W = spr.width();
  char buf[40];
  spr.fillSprite(C_BG());
  int best = bestChannel();
  if (best) snprintf(buf, sizeof(buf), L("best channel %d", "лучший канал %d"), best); else buf[0] = 0;
  uiHeader(L("Channels", "Каналы"), buf);
  scanDot();
  if (waiting()) { present(); return; }

  int pl = UI_M + 24, pr = W - UI_M - 8, pt = UI_TOP + 4, pb = UI_BOTTOM - 4;
  auto chX = [&](float ch) { return pl + (int)((ch - 1) * (pr - pl) / (CH_MAX - 1)); };
  auto dbY = [&](int db) { int c = constrain(db, -100, -30); return pb - (c + 100) * (pb - pt) / 70; };

  for (int db = -90; db <= -40; db += 10) {
    int y = dbY(db);
    for (int x = pl; x < pr; x += 4) spr.drawPixel(x, y, UI_LINE());
    snprintf(buf, sizeof(buf), "%d", db);
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(UI_DIM());
    txt(buf, pl - 4, y, 1);
  }
  spr.drawFastHLine(pl, pb, pr - pl, UI_LABEL());
  for (int c = 1; c <= CH_MAX; c++) {
    bool main3 = (c == 1 || c == 6 || c == 11);
    spr.drawFastVLine(chX(c), pb, 3, main3 ? UI_LABEL() : UI_LINE());
    snprintf(buf, sizeof(buf), "%d", c);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(c == best ? C_ACC() : main3 ? UI_LABEL() : UI_DIM());
    txt(buf, chX(c), pb + 5, 1);
  }
  if (best) spr.fillTriangle(chX(best), pb + 14, chX(best) - 4, pb + 19, chX(best) + 4, pb + 19, C_ACC());

  // humps of real width (20 / 40 MHz), weak ones at the bottom, heat = strength
  static std::vector<std::pair<int, uint64_t>> v;      // static: no allocation every frame
  v.clear();
  for (auto& kv : g_aps) if (kv.second.ch >= 1 && kv.second.ch <= CH_MAX) v.push_back({ kv.second.rssi, kv.first });
  std::sort(v.begin(), v.end());
  for (auto& p : v) {
    const Ap& a = g_aps[p.second];
    uint16_t c = heat(a.rssi), fill = scale565(c, 18);
    int top = dbY(a.rssi);
    float cen = apCenter(a), half = apHalf(a);
    int prevx = -1, prevy = -1;
    for (int s = 0; s <= 24; s++) {
      float f = -1.0f + s / 12.0f;
      int x = chX(cen + f * half);
      int y = pb - (int)((pb - top) * (1.0f - f * f));
      if (x < pl || x > pr) { prevx = -1; continue; }
      if (y < pb) spr.drawFastVLine(x, y, pb - y, fill);
      if (prevx >= 0) spr.drawLine(prevx, prevy, x, y, c);
      prevx = x; prevy = y;
    }
  }
  for (int i = (int)v.size() - 1, k = 0; i >= 0 && k < 3; i--, k++) {     // labels of the three strongest
    const Ap& a = g_aps[v[i].second];
    char nm[20], nu[20];
    fitText(ssidOr(a), nm, sizeof(nm), 72, 1);
    upperCopy(nm, nu, sizeof(nu));
    spr.setTextDatum(BC_DATUM);
    spr.setTextColor(C_HOT());
    txt(nu, constrain(chX(apCenter(a)), pl + 30, pr - 30), dbY(a.rssi) - 2, 1);
  }
  present();
}

// ============================================================
//  ACTIVITY — packets per second and noise per channel
// ============================================================
void drawActivity(uint32_t now) {
  int W = spr.width();
  char buf[40];
  float tot = 0, mx = 1;
  for (int c = 1; c <= CH_MAX; c++) { float t = g_ppsMgmt[c] + g_ppsData[c]; tot += t; mx = max(mx, t); }
  spr.fillSprite(C_BG());
  snprintf(buf, sizeof(buf), L("%d pkt/s", "%d пак/с"), (int)(tot + 0.5f));
  uiHeader(L("Air activity", "Активность эфира"), buf);
  scanDot();

  int pl = UI_M + 40, pr = W - UI_M - 6, pt = UI_TOP + 2, pb = UI_TOP + 64;
  int bw = (pr - pl) / CH_MAX;
  float smx = sqrtf(mx);
  for (int c = 1; c <= CH_MAX; c++) {
    int x = pl + (c - 1) * bw;
    float t = g_ppsMgmt[c] + g_ppsData[c];
    int h = (int)((pb - pt) * sqrtf(t) / smx);
    int hd = t > 0 ? (int)(h * g_ppsData[c] / t) : 0;
    spr.fillRect(x + 2, pt, bw - 4, pb - pt, TC(10, 18, 26));
    if (h > 0) {                                  // data bright, management dimmer
      uint16_t hc = heatV(0.3f + 0.7f * sqrtf(t) / smx);
      spr.fillRect(x + 2, pb - h, bw - 4, h - hd, scale565(hc, 40));
      spr.fillRect(x + 2, pb - hd, bw - 4, hd, hc);
    }
    if (c == g_hopCh) spr.fillRect(x + 2, pb + 1, bw - 4, 2, C_ACC());
    snprintf(buf, sizeof(buf), "%d", c);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor((c == 1 || c == 6 || c == 11) ? UI_LABEL() : UI_DIM());
    txt(buf, x + bw / 2, pb + 5, 1);
    if (g_noise[c] != 0) {                         // noise: usually -90..-95 dBm; above -85 = alert
      int nf = (int)lroundf(g_noise[c]);
      snprintf(buf, sizeof(buf), "%d", -nf);
      spr.setTextColor(nf > -86 ? C_ALERT() : UI_DIM());
      txt(buf, x + bw / 2, pb + 16, 1);
    }
  }
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_DIM());
  txt(L("CH", "КАН"), UI_M + 8, pb + 5, 1);
  txt(L("NOISE", "ШУМ"), UI_M + 2, pb + 16, 1);
  txt(L("DATA", "ДАН."), UI_M + 10, pt + 2, 1);
  spr.fillRect(UI_M + 2, pt + 2, 5, 7, heatV(0.9f));
  txt(L("MGMT", "СЛУЖ"), UI_M + 10, pt + 14, 1);
  spr.fillRect(UI_M + 2, pt + 14, 5, 7, scale565(heatV(0.9f), 40));

  // total traffic over 2 minutes
  int gx = UI_M + 4, gy = UI_TOP + 92, gw = W - 2 * UI_M - 8, gh = UI_BOTTOM - gy + 2;
  uiFrame(gx, gy, gw, gh, UI_LINE());
  float hm = 1;
  for (int i = 0; i < g_actHistN; i++) hm = max(hm, g_actHist[i]);
  for (int i = 0; i < g_actHistN; i++) {
    int xx = gx + 2 + (gw - 5) * (i + (ACT_HIST_N - g_actHistN)) / (ACT_HIST_N - 1);
    int h = (int)((gh - 5) * g_actHist[i] / hm);
    spr.drawFastVLine(xx, gy + gh - 2 - h, h, heatV(0.25f + 0.6f * g_actHist[i] / hm));
  }
  snprintf(buf, sizeof(buf), L("2 MIN  ·  PEAK %d", "2 МИН  ·  МАКС %d"), (int)hm);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_DIM());
  txt(buf, gx + 5, gy + 3, 1);
  present();

  if (g_ledEvent == 0) {                           // LED strip = traffic level
    int lv = tot < 1 ? 0 : constrain((int)(log10f(tot) * 2.7f), 0, WS2812_N);
    strip.clear();
    for (int i = 0; i < lv; i++) strip.setPixelColor(i, strip.Color(10 + i * 8, 40 + i * 20, 60 + i * 20));
    strip.show();
  }
}

// ============================================================
//  CLIENTS — devices per network
// ============================================================
void drawClients(uint32_t now) {
  int W = spr.width();
  char buf[48];
  static std::map<uint64_t, int> per;
  per.clear();
  for (auto& kv : g_stas) per[kv.second.bssid]++;
  static std::vector<std::pair<int, uint64_t>> v;
  v.clear();
  for (auto& kv : per) v.push_back({ -kv.second, kv.first });
  std::sort(v.begin(), v.end());

  spr.fillSprite(C_BG());
  snprintf(buf, sizeof(buf), L("%d devices  ·  %d networks", "%d устр.  ·  %d сетей"), (int)g_stas.size(), (int)per.size());
  uiHeader(L("Clients", "Клиенты"), buf);
  scanDot();

  if (v.empty()) uiEmpty(L("listening", "слушаю"), L("devices appear as they send data", "устройства появятся, когда начнут передавать"));
  int rows = 5, rh = 17, y0 = UI_TOP + 2;
  int maxc = v.empty() ? 1 : -v[0].first;
  for (int i = 0; i < rows && i < (int)v.size(); i++) {
    uint64_t b = v[i].second;
    int cnt = -v[i].first, cy = y0 + i * rh + rh / 2;
    auto it = g_aps.find(b);
    const char* nm;
    char vb[24];
    if (it != g_aps.end()) nm = ssidOr(it->second);
    else {
      uint8_t m[6]; for (int k = 0; k < 6; k++) m[k] = (b >> (8 * (5 - k))) & 0xFF;
      const char* ven = ouiName(m);
      snprintf(vb, sizeof(vb), "? %s", ven ? ven : L("unknown network", "неизвестная сеть"));
      nm = vb;
    }
    fitText(nm, buf, sizeof(buf), 124, 2);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(it != g_aps.end() ? C_HOT() : UI_DIM());
    txt(buf, UI_M + 8, cy, 2);
    int bx = 144, bwid = W - bx - UI_M - 28;
    int len = max(4, bwid * cnt / maxc);
    for (int x = 0; x + 3 <= len; x += 5) spr.fillRect(bx + x, cy - 4, 3, 8, heatV(0.4f + 0.6f * cnt / maxc));
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(C_HOT());
    txtNum(cnt, W - UI_M - 8, cy, 2);
  }
  // "looking for Wi-Fi"
  int y = UI_TOP + 2 + rows * rh + 4;
  spr.drawFastHLine(UI_M + 8, y, W - 2 * UI_M - 16, UI_LINE());
  snprintf(buf, sizeof(buf), "%d", (int)g_probes.size());
  uiReadout(UI_M + 8, y + 4, L("looking for Wi-Fi", "ищут Wi-Fi"), buf, C_ACC(), 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(UI_DIM());
  txt(L("LAST 2 MIN, APPROXIMATE:", "ЗА 2 МИН, ПРИМЕРНО:"), W - UI_M - 8, y + 6, 1);
  txt(L("PHONES CHANGE ADDRESSES", "ТЕЛЕФОНЫ МЕНЯЮТ АДРЕСА"), W - UI_M - 8, y + 16, 1);
  present();
}

// ============================================================
//  FIND — hot/cold on the selected network
// ============================================================
void drawFind(uint32_t now) {
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  uint64_t t = findTarget();
  if (!t) { uiHeader(L("Finder", "Поиск")); waiting(); present(); ledsOff(); return; }
  const Ap& a = g_aps[t];
  char sub[40]; snprintf(sub, sizeof(sub), L("ch %d  ·  %s", "к %d  ·  %s"), a.ch, encName(a.enc));
  uiHeader(L("Finder", "Поиск"), sub);

  // average signal over the last ~5 samples
  int sum = 0, cnt = 0;
  for (int i = g_fSparkN - 1; i >= 0 && i >= g_fSparkN - 5; i--)
    if (g_fSpark[i] > -127) { sum += g_fSpark[i]; cnt++; }
  bool lost = (cnt == 0) || (msSince(now, g_fLastHit) > 4000);
  int r = cnt ? sum / cnt : a.rssi;
  const char* word = lost ? L("NO SIGNAL", "НЕТ СИГНАЛА") : r > -55 ? L("EXCELLENT", "ОТЛИЧНО") : r > -67 ? L("GOOD", "ХОРОШО") : r > -75 ? L("FAIR", "СРЕДНЕ") : r > -85 ? L("WEAK", "СЛАБО") : L("BAD", "ПЛОХО");
  uint16_t col = lost ? UI_DIM() : heat(r);
  int pct = lost ? 0 : constrain((r + 95) * 100 / 60, 0, 100);

  int cx = W / 2, cy = 120;
  drawArcGauge(cx, cy, 72, 9, pct, col, UI_LINE(), C_BG());
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(lost ? UI_DIM() : C_HOT());
  if (lost) txt("--", cx, cy - 22, 6); else txtNum(r, cx, cy - 22, 6);
  if (!lost) {
    float d = powf(10.0f, (-45.0f - r) / 25.0f);    // rough distance estimate to the AP
    snprintf(buf, sizeof(buf), L("DBM  %s  ~%.0f M", "ДБМ  %s  ~%.0f М"), word, d < 1 ? 1.0f : d);
  } else snprintf(buf, sizeof(buf), "%s", word);
  spr.setTextColor(UI_LABEL());
  txt(buf, cx, cy + 4, 1);
  char nm[40]; fitText(ssidOr(a), nm, sizeof(nm), 200, 2);
  spr.setTextColor(C_HOT());
  txt(nm, cx, cy + 22, 2);

  // left: MAX / MIN / AVG; right: trend
  int mx = -127, mn = 0, s2 = 0, n = 0;
  for (int i = 0; i < g_fSparkN; i++) if (g_fSpark[i] > -127) {
    mx = max(mx, (int)g_fSpark[i]); mn = min(mn, (int)g_fSpark[i]); s2 += g_fSpark[i]; n++;
  }
  if (n) {
    snprintf(buf, sizeof(buf), "%d", mx); uiReadout(UI_M + 8, UI_TOP + 4, L("max", "макс"), buf, C_HOT(), 2);
    snprintf(buf, sizeof(buf), "%d", mn); uiReadout(UI_M + 8, UI_TOP + 32, L("min", "мин"), buf, C_HOT(), 2);
    snprintf(buf, sizeof(buf), "%d", s2 / n); uiReadout(UI_M + 8, UI_TOP + 60, L("avg", "сред"), buf, C_HOT(), 2);
  }
  int recent = 0, rn = 0, older = 0, on = 0;
  for (int i = g_fSparkN - 1; i >= 0 && i >= g_fSparkN - 5; i--) if (g_fSpark[i] > -127) { recent += g_fSpark[i]; rn++; }
  for (int i = g_fSparkN - 6; i >= 0 && i >= g_fSparkN - 10; i--) if (g_fSpark[i] > -127) { older += g_fSpark[i]; on++; }
  if (rn && on) {
    int dlt = recent / rn - older / on;
    snprintf(buf, sizeof(buf), L("%+d dB", "%+d дБ"), dlt);
    int tx = W - UI_M - 64;
    uiReadout(tx, UI_TOP + 4, dlt >= 3 ? L("warmer", "теплее") : dlt <= -3 ? L("colder", "холоднее") : L("trend", "тренд"), buf, dlt >= 3 ? C_ACC() : C_HOT(), 2);
  }
  uiFooter(L("wheel: screens  ·  click: next network", "колесо: экраны · нажать: сеть"), L("side: back", "кнопка: назад"));
  present();
  if (!lost && g_wifiTicks) audioGeiger(pct);                     // clicks get faster as you get closer

  if (g_ledEvent == 0) {                           // LED strip = signal level
    int lv = pct * WS2812_N / 100;
    strip.clear();
    uint8_t rr = ((col >> 11) & 0x1F) << 3, gg = ((col >> 5) & 0x3F) << 2, bb = (col & 0x1F) << 3;
    for (int i = 0; i < lv; i++) strip.setPixelColor(i, strip.Color(rr / 3, gg / 3, bb / 3));
    strip.show();
  }
}
