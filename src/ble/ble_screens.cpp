// ============================================================
//  ble_screens.cpp — Bluetooth section screens
//
//  OVERVIEW · SCOPE · NEAREST · DEVICES (+details) · WEATHER
//  All built from core/ui.h components on a common grid.
// ============================================================
#include <algorithm>
#include "core/lang.h"
#include "core/timeutil.h"
#include "ble/ble_model.h"
#include "ble/vendors.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/settings.h"
#include "audio/audio.h"

static int g_sweep = 0;                  // beam angle on SCOPE

// user label beats the over-the-air name; Cyrillic uses the font with Russian letters
static bool isUtf8(const char* s) { for (; *s; s++) if ((uint8_t)*s >= 0x80) return true; return false; }
static int  fontFor(const char* s, int latin) { return isUtf8(s) ? (latin >= 4 ? 5 : 3) : latin; }

float estDistance(int rssi) {
  const float MEASURED = -59.0f;         // expected RSSI at 1 m
  const float N = 2.5f;                  // path-loss exponent (indoors)
  float d = powf(10.0f, (MEASURED - (float)rssi) / (10.0f * N));
  return constrain(d, 0.1f, 99.0f);
}


static const char* akindName(uint8_t k) {
  switch (k) {
    case A_PUBLIC:   return "public";
    case A_STATIC:   return "static";
    case A_ROTATING: return "rotating";
    default:         return "random";
  }
}

static const char* uuidName(uint16_t u) {
  switch (u) {
    case 0x180F: return "Battery";   case 0x180A: return "DevInfo";
    case 0x1800: return "GAP";       case 0x1801: return "GATT";
    case 0xFE2C: return "FastPair";  case 0xFD6F: return "ExpNotif";
    case 0xFEAA: return "Eddystone"; case 0x181A: return "EnvSens";
    case 0xFCD2: return "BTHome";    case 0xFE95: return "Xiaomi";
    case 0x1812: return "HID";       case 0x180D: return "HeartRate";
    case 0xFEED: case 0xFEEC: return "Tile";
    case 0xFD5A: return "SmartTag";  case 0xFE9F: return "Google";
    case 0xFD3D: return "SwitchBot"; case 0xFDCD: return "Qingping";
    case 0x1809: return "Thermo";    case 0x184E: return "LEAudio";
    case 0xFE33: return "Chipolo";   case 0xFE07: return "Sonos";
    case 0xFE03: return "Amazon";    case 0xFD5F: return "Meta";
    default:     return "";
  }
}

static void upperCopy(const char* s, char* out, size_t n) { utf8Upper(s, out, n); }

// types by descending count
static int sortedTypes(const int* cnt, int* order) {
  int n = 0;
  for (int t = 0; t < T_NUM; t++) if (cnt[t] > 0) order[n++] = t;
  std::sort(order, order + n, [&](int a, int b) { return cnt[a] > cnt[b]; });
  return n;
}

// ============================================================
//  OVERVIEW — how many around, how it changed, who they are
// ============================================================
void drawOverview(int count, int raw, const int* typeCnt) {
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  snprintf(buf, sizeof(buf), L("%d MAC  ·  range %s", "%d MAC  ·  дальность %s"), raw, RANGES[g_range].name);
  uiHeader(L("Overview", "Обзор"), buf);

  // left: main number and trend
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_HOT());
  txtNum(count, UI_M + 8, UI_TOP + 2, 6);
  spr.setTextColor(UI_DIM());
  txt(PL(count, "DEVICE", "DEVICES", "УСТРОЙСТВО", "УСТРОЙСТВА", "УСТРОЙСТВ"), UI_M + 8, UI_TOP + 50, 1);
  int delta = 0;                                   // change over ~2 minutes
  if (g_histLen > 120) delta = g_hist[g_histLen - 1].count - g_hist[g_histLen - 121].count;
  snprintf(buf, sizeof(buf), "%+d", delta);
  uiReadout(UI_M + 8, UI_TOP + 64, L("2 min", "2 мин"), buf, delta > 0 ? C_ACC() : UI_LABEL(), 2);

  // right: device count chart
  int gx = 112, gy = UI_TOP + 2, gw = W - UI_M - 4 - gx, gh = 82;
  uiFrame(gx, gy, gw, gh, UI_LINE());
  int peak = 1;
  for (int i = 0; i < g_histLen; i++) peak = max(peak, g_hist[i].count);
  int ymax = peak + peak / 5 + 1;
  float step = (float)(gw - 4) / (HIST_N - 1);
  int px = -1, py = -1;
  for (int i = 0; i < g_histLen; i++) {
    int x = gx + gw - 3 - (int)((g_histLen - 1 - i) * step);
    int y = gy + gh - 3 - (int)((long)g_hist[i].count * (gh - 8) / ymax);
    spr.drawFastVLine(x, y, gy + gh - 3 - y, UI_PANEL());
    if (px >= 0) spr.drawLine(px, py, x, y, C_ACC());
    if (g_hist[i].ev > 0) spr.fillTriangle(x, gy + 18, x - 3, gy + 13, x + 3, gy + 13, C_HOT());   // "a group arrived"
    px = x; py = y;
  }
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_DIM());
  txt(L("COUNT · 2.5 MIN", "ЧИСЛО · 2.5 МИН"), gx + 6, gy + 4, 1);
  snprintf(buf, sizeof(buf), L("PEAK %d", "МАКС %d"), peak);
  spr.setTextDatum(TR_DATUM);
  txt(buf, gx + gw - 6, gy + 4, 1);

  // bottom: what the crowd is made of: share bar and icons with counts
  int order[T_NUM];
  int n = sortedTypes(typeCnt, order), total = 0;
  for (int i = 0; i < n; i++) total += typeCnt[order[i]];
  int by = UI_TOP + 92, bx = UI_M + 8, bw = W - 2 * UI_M - 16;
  if (total > 0) {
    int x = bx;
    for (int i = 0; i < n; i++) {
      int t = order[i];
      int w = (i == n - 1) ? bx + bw - x : bw * typeCnt[t] / total;
      if (w < 2) w = 2;
      uint16_t c = t == T_TRACKER ? C_ALERT() : heatV(0.9f - 0.12f * i);
      spr.fillRect(x, by, w - 1, 5, c);
      x += w;
    }
    for (int i = 0; i < n && i < 6; i++) {
      int t = order[i], cx = bx + 4 + i * 50, cy = by + 20;
      bool alert = (t == T_TRACKER);
      drawGlyph(cx, cy, typeGlyph(t), alert ? C_ALERT() : UI_LABEL(), alert);
      spr.setTextDatum(ML_DATUM);
      spr.setTextColor(alert ? C_ALERT() : C_HOT());
      txtNum(typeCnt[t], cx + 9, cy, 2);
    }
  } else {
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(UI_DIM());
    txt(L("NO DEVICES IN RANGE", "НЕТ УСТРОЙСТВ В ЗОНЕ"), bx, by + 14, 1);
  }
  uiFooter(L("wheel: screens", "колесо: экраны"), L("side: back", "кнопка: назад"));
  present();
}

// ============================================================
//  SCOPE — Evangelion AT-Field Octagram × Zen Enso Sanctuary Radar
// ============================================================
void drawScope(const std::vector<Blip>& bl) {
  int W = spr.width();
  char buf[32];
  spr.fillSprite(C_BG());
  bool hasTrk = false;
  for (size_t i = 0; i < bl.size(); i++) if (bl[i].type == T_TRACKER) { hasTrk = true; break; }
  snprintf(buf, sizeof(buf), hasTrk ? L("%d · TRACKER!", "%d · ТРЕКЕР!") : "%d %s",
           (int)bl.size(), PL((int)bl.size(), "device", "devices", "устройство", "устройства", "устройств"));
  uiHeader(L("Scope", "Радар"), buf, hasTrk);

  // Right: Octagonal AT-Field + Enso Radar
  int maxR = 59, cx = W - UI_M - 8 - maxR, cy = UI_TOP + 63;
  uint16_t ring = UI_LINE(), acc = C_ACC(), hot = C_HOT();

  // Outer 8-vertex Evangelion AT-Field octagram frame
  for (int k = 0; k < 8; k++) {
    float a0 = (k * 45.0f + 22.5f) * PI / 180.0f, a1 = ((k + 1) * 45.0f + 22.5f) * PI / 180.0f;
    int rOct = maxR + 3;
    spr.drawLine(cx + (int)lroundf(cosf(a0) * rOct), cy + (int)lroundf(sinf(a0) * rOct),
                 cx + (int)lroundf(cosf(a1) * rOct), cy + (int)lroundf(sinf(a1) * rOct),
                 hasTrk ? C_ALERT() : scale565(acc, 65));
  }
  // 24 precision azimuth compass ticks
  for (int d = 0; d < 360; d += 15) {
    float a = d * PI / 180.0f;
    int r0 = maxR, r1 = (d % 90 == 0) ? maxR + 5 : maxR + 2;
    spr.drawLine(cx + (int)lroundf(cosf(a) * r0), cy + (int)lroundf(sinf(a) * r0),
                 cx + (int)lroundf(cosf(a) * r1), cy + (int)lroundf(sinf(a) * r1),
                 (d % 90 == 0) ? acc : ring);
  }
  // Concentric range circles + diagonal Dharma axes
  for (int k = 1; k <= 3; k++) spr.drawCircle(cx, cy, maxR * k / 3, k == 3 ? scale565(acc, 55) : ring);
  spr.drawFastHLine(cx - maxR, cy, 2 * maxR, ring);
  spr.drawFastVLine(cx, cy - maxR, 2 * maxR, ring);
  int diag = (int)(maxR * 0.707f);
  spr.drawLine(cx - diag, cy - diag, cx + diag, cy + diag, scale565(ring, 65));
  spr.drawLine(cx - diag, cy + diag, cx + diag, cy - diag, scale565(ring, 65));

  // Volumetric phosphor sweep fan + bright leading blade
  for (int t = 1; t < 44; t++) {
    float a = (g_sweep - t) * PI / 180.0f;
    float k = 1.0f - t / 44.0f;
    spr.drawLine(cx, cy, cx + (int)(cosf(a) * (maxR - 1)), cy + (int)(sinf(a) * (maxR - 1)), heatV(0.12f + 0.52f * k * k));
  }
  {
    float a0 = g_sweep * PI / 180.0f;
    spr.drawLine(cx, cy, cx + (int)(cosf(a0) * maxR), cy + (int)(sinf(a0) * maxR), hot);
  }
  // Central Tengen Eye diamond
  spr.fillRect(cx - 1, cy - 1, 3, 3, acc);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_DIM());
  txt("1M", cx + 3, cy - maxR / 3 + 2, 1);
  txt("5M", cx + 3, cy - 2 * maxR / 3 + 2, 1);

  // Targets: weak below, strong on top; sweep hit draws hexagonal/diamond ping halo
  static std::vector<int> idx;
  idx.resize(bl.size());
  for (size_t i = 0; i < bl.size(); i++) idx[i] = (int)i;
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return bl[a].rssi < bl[b].rssi; });
  for (int i : idx) {
    int deg = (int)(bl[i].h % 360);
    float ang = deg * PI / 180.0f;
    int pct = constrain((bl[i].rssi + 95) * 100 / 50, 0, 100);
    int r = maxR - (maxR - 7) * pct / 100;
    int tx = cx + (int)(cosf(ang) * r), ty = cy + (int)(sinf(ang) * r);
    bool trk = (bl[i].type == T_TRACKER);
    int since = (g_sweep - deg + 360) % 360;
    if (since < 36) {
      int pr = 4 + since / 9;
      uint16_t pc = trk ? C_ALERT() : acc;
      spr.drawLine(tx, ty - pr, tx + pr, ty, pc);
      spr.drawLine(tx + pr, ty, tx, ty + pr, pc);
      spr.drawLine(tx, ty + pr, tx - pr, ty, pc);
      spr.drawLine(tx - pr, ty, tx, ty - pr, pc);
    }
    drawMarker(tx, ty, heat(bl[i].rssi), trk);
  }

  // Left: Chamfered MAGI Tactical Class Breakdown Panel
  int px = UI_M, py = UI_TOP + 2, pw = cx - maxR - UI_M - 12, ph = UI_BOTTOM - py - 2;
  hudPanel(px, py, pw, ph, 8, UI_LINE(), false, C_BG());
  spr.fillRect(px + 8, py, 32, 2, hasTrk ? C_ALERT() : acc);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(acc);
  txt(L("// ETHER CLASSES", "// КЛАССЫ ЭФИРА"), px + 7, py + 5, 1);

  int cnt[T_NUM] = {0};
  for (size_t i = 0; i < bl.size(); i++) cnt[bl[i].type < T_NUM ? (int)bl[i].type : 0]++;
  int order[T_NUM];
  int n = sortedTypes(cnt, order);
  int maxC = n > 0 ? max(1, cnt[order[0]]) : 1;
  if (n == 0) {
    spr.setTextColor(UI_DIM());
    txt(L("LISTENING...", "СЛУШАЮ ЭФИР..."), px + 7, py + 24, 1);
  }
  for (int i = 0; i < n && i < 6; i++) {
    int t = order[i], y = py + 23 + i * 17;
    bool alert = (t == T_TRACKER);
    drawGlyph(px + 12, y, typeGlyph(t), alert ? C_ALERT() : UI_LABEL(), alert);
    char nm[16], fitN[16]; upperCopy(typeName(t), nm, sizeof(nm));
    fitText(nm, fitN, sizeof(fitN), pw - 66, 1);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(alert ? C_ALERT() : UI_LABEL());
    txt(fitN, px + 21, y, 1);
    // Mini segmented bar
    int bx0 = px + pw - 42, bw0 = 18, fillW = max(2, bw0 * cnt[t] / maxC);
    spr.fillRect(bx0, y - 2, bw0, 4, ring);
    spr.fillRect(bx0, y - 2, fillW, 4, alert ? C_ALERT() : acc);
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(alert ? C_ALERT() : hot);
    txtNum(cnt[t], px + pw - 6, y, 2);
  }
  uiFooter(L("wheel: screens", "колесо: экраны"), L("side: back", "кнопка: назад"));
  present();
  g_sweep = (g_sweep + 6) % 360;
}

// ============================================================
//  NEAREST — closest device on an arc scale
// ============================================================
void drawNearest(bool have, int rssi, const Ent& best, int count) {
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  snprintf(buf, sizeof(buf), L("%d in range", "%d в зоне"), count);
  uiHeader(L("Nearest", "Ближайшее"), buf);
  if (!have) { uiEmpty(L("no devices", "нет устройств"), L("nothing heard in range", "в зоне ничего не слышно")); present(); return; }

  int pct = constrain((rssi + 95) * 100 / 50, 0, 100);
  const char* word = pct > 66 ? L("CLOSE", "ВПЛОТНУЮ") : pct > 33 ? L("NEAR", "РЯДОМ") : L("FAR", "ДАЛЕКО");
  bool tracker = (best.type == T_TRACKER);
  uint16_t col = tracker ? C_ALERT() : heat(rssi);
  int cx = W / 2, cy = 120;
  drawArcGauge(cx, cy, 72, 9, pct, col, UI_LINE(), C_BG());

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_HOT());
  txtNum(rssi, cx, cy - 22, 6);
  float d = estDistance(rssi);
  if (d < 10.0f) snprintf(buf, sizeof(buf), L("DBM  %s  ~%.1f M", "ДБМ  %s  ~%.1f М"), word, d);
  else           snprintf(buf, sizeof(buf), L("DBM  %s  ~%.0f M", "ДБМ  %s  ~%.0f М"), word, d);
  spr.setTextColor(UI_LABEL());
  txt(buf, cx, cy + 4, 1);

  // who is it
  char tn[16]; upperCopy(typeName(best.type), tn, sizeof(tn));
  char who[40]; snprintf(who, sizeof(who), "%s  %s", tn, vendorLabel(best.cat, best.vkind, best.vcode));
  const char* nm = best.label[0] ? best.label : best.name[0] ? best.name : who;
  int nf = fontFor(nm, 2);
  char fit[48]; fitText(nm, fit, sizeof(fit), 230, nf);
  spr.setTextColor(tracker ? C_ALERT() : C_HOT());
  txt(fit, cx, cy + 22, nf);
  if (best.label[0] || best.name[0]) {
    char w2[40]; upperCopy(who, w2, sizeof(w2));
    spr.setTextColor(UI_DIM());
    txt(w2, cx, cy + 36, 1);
  }
  if (tracker) {
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_ALERT());
    txt(L("! TRACKER", "! ТРЕКЕР"), UI_M + 8, UI_TOP + 4, 2);
  }
  present();
  if (g_bleTicks) audioGeiger(pct);                // clicks faster = closer
}

// ============================================================
//  DEVICES — list (nearest first) and details
// ============================================================
void drawList(const std::map<uint32_t, Ent>& ents) {
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  snprintf(buf, sizeof(buf), L("%d  ·  nearest first", "%d  ·  ближние сверху"), (int)g_order.size());
  uiHeader(L("Devices", "Устройства"), buf);
  if (g_order.empty()) { uiEmpty(L("no devices", "нет устройств"), L("nothing heard in range", "в зоне ничего не слышно")); uiFooter("", L("side: back", "кнопка: назад")); present(); return; }

  for (int i = 0; i < LIST_ROWS; i++) {
    int idx = g_top + i;
    if (idx >= (int)g_order.size()) break;
    uint32_t id = g_order[idx];
    auto it = ents.find(id);
    int y = LIST_Y0 + i * LIST_ROWH, cy = y + LIST_ROWH / 2 - 1;
    bool sel = (id == g_sel);
    if (sel) uiRowHighlight(y, LIST_ROWH - 1);
    if (it == ents.end()) {
      spr.setTextDatum(ML_DATUM);
      spr.setTextColor(UI_DIM());
      txt(L("(gone)", "(пропало)"), UI_M + 18, cy, 2);
      continue;
    }
    const Ent& e = it->second;
    bool sep = (e.flags & DF_SEPARATED) != 0;
    bool alert = (e.type == T_TRACKER) || sep;
    drawGlyph(UI_M + 8, cy, typeGlyph(e.type), alert ? C_ALERT() : (sel ? C_ACC() : UI_LABEL()), alert);
    const char* nm = e.label[0] ? e.label : e.name[0] ? e.name : e.model[0] ? e.model : typeName(e.type);
    int nf = fontFor(nm, 2);
    fitText(nm, buf, sizeof(buf), 112, nf);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(alert ? C_ALERT() : (sel || e.label[0] || e.name[0] ? C_HOT() : UI_LABEL()));
    txt(buf, UI_M + 18, cy, nf);
    char vb[20];
    fitText(vendorLabel(e.cat, e.vkind, e.vcode), vb, sizeof(vb), 56, 1);
    spr.setTextColor((e.flags & DF_ACTIVE) ? C_ACC() : UI_DIM());
    txt(vb, 136, cy, 1);
    float dist = estDistance(e.rssi);
    if (dist < 10.0f) snprintf(buf, sizeof(buf), "~%.1fm", dist);
    else              snprintf(buf, sizeof(buf), "~%.0fm", dist);
    spr.setTextColor(UI_LABEL());
    txt(buf, 196, cy, 1);
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(alert ? C_ALERT() : heat(e.rssi));
    txtNum(e.rssi, 268, cy, 2);
    drawBars(274, cy - 7, e.rssi, alert ? C_ALERT() : heat(e.rssi));
  }
  int n = (int)g_order.size();                     // scrollbar
  if (n > LIST_ROWS) {
    int trackH = LIST_ROWS * LIST_ROWH;
    int barH = max(8, trackH * LIST_ROWS / n);
    int barY = LIST_Y0 + (trackH - barH) * g_top / (n - LIST_ROWS);
    spr.fillRect(W - UI_M + 2, LIST_Y0, 2, trackH, UI_LINE());
    spr.fillRect(W - UI_M + 2, barY, 2, barH, C_ACC());
  }
  uiFooter(L("wheel: pick  ·  click: details", "колесо: выбор · нажать: подробно"), L("side: back", "кнопка: назад"));
  present();
}

void drawDetail(const Dev& d, const Ent& e, bool inMap, uint32_t now) {
  int W = spr.width();
  char buf[96], tmp[48];
  spr.fillSprite(C_BG());
  int pos = -1;
  for (size_t i = 0; i < g_order.size(); i++) if (g_order[i] == g_sel) pos = (int)i;
  if (pos >= 0) snprintf(buf, sizeof(buf), L("%d / %d  ·  click: list", "%d / %d · нажать: список"), pos + 1, (int)g_order.size());
  else          snprintf(buf, sizeof(buf), "%s", L("click: list", "нажать: список"));
  uiHeader(L("Device", "Устройство"), buf);

  // name and "who is it"
  bool alert = (e.type == T_TRACKER);
  const char* nm = e.label[0] ? e.label : d.name[0] ? d.name : typeName(e.type);
  int nf = fontFor(nm, 4);
  fitText(nm, tmp, sizeof(tmp), W - 2 * UI_M - 24, nf);
  drawGlyph(UI_M + 8, UI_TOP + 11, typeGlyph(e.type), alert ? C_ALERT() : UI_LABEL(), alert);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(alert ? C_ALERT() : C_HOT());
  txt(tmp, UI_M + 20, UI_TOP, nf);
  char tn[16]; upperCopy(typeName(e.type), tn, sizeof(tn));
  if (e.vkind) snprintf(buf, sizeof(buf), "%s  %s  %s%04X", tn, vendorLabel(e.cat, e.vkind, e.vcode), e.vkind == 1 ? "0x" : "svc ", e.vcode);
  else         snprintf(buf, sizeof(buf), L("%s  ·  maker unknown", "%s  ·  производитель неизвестен"), tn);
  char bu[96]; upperCopy(buf, bu, sizeof(bu));
  spr.setTextColor(UI_LABEL());
  txt(bu, UI_M + 20, UI_TOP + 26, 1);

  // readouts: 3 columns x 2 rows
  uint32_t ago = msSince(now, e.t) / 1000;
  int x0 = UI_M + 8, y0 = UI_TOP + 40, cw = 64;
  if (!inMap) { uiReadout(x0, y0, L("signal", "сигнал"), L("lost", "потерян"), C_ALERT()); }
  else { snprintf(tmp, sizeof(tmp), "%d", e.rssi); uiReadout(x0, y0, ago >= 3 ? L("signal (old)", "сигнал (давно)") : L("signal", "сигнал"), tmp, heat(e.rssi)); }
  float dist = estDistance(e.rssi);
  if (dist < 10) snprintf(tmp, sizeof(tmp), L("~%.1fm", "~%.1fм"), dist); else snprintf(tmp, sizeof(tmp), L("~%.0fm", "~%.0fм"), dist);
  uiReadout(x0 + cw, y0, L("distance", "до него"), tmp, C_HOT());
  uint32_t secs = (now - (e.first ? e.first : now)) / 1000;
  snprintf(tmp, sizeof(tmp), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  uiReadout(x0 + 2 * cw, y0, L("seen", "виден"), tmp, C_HOT());
  snprintf(tmp, sizeof(tmp), "%d/%d", d.rmin, d.rmax);
  uiReadout(x0, y0 + 28, L("range", "разброс"), tmp, C_HOT());
  snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)e.pkts);
  uiReadout(x0 + cw, y0 + 28, L("packets", "пакеты"), tmp, C_HOT());
  snprintf(tmp, sizeof(tmp), "%d", e.nMacs);
  uiReadout(x0 + 2 * cw, y0 + 28, L("addresses", "адреса"), tmp, C_HOT());

  // signal chart
  int gx = 206, gy = y0, gw = W - UI_M - 4 - gx, gh = 52;
  uiFrame(gx, gy, gw, gh, UI_LINE());
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(UI_DIM());
  txt(L("SIGNAL · 30 S", "СИГНАЛ · 30 С"), gx + 5, gy + 3, 1);
  drawSpark(gx, gy, gw, gh, g_spark, g_sparkN, SPARK_N, heat(e.rssi));

  // technical details + decoded passive intelligence (3 strict 10px lines, fitted to width -> 0 overlap)
  int y = UI_TOP + 98;
  int maxW = W - 2 * UI_M - 16;
  int n = 0;
  auto addN = [&](int w) { if (w > 0) n += min(w, (int)sizeof(buf) - 1 - n); };
  buf[0] = 0;
  if (e.flags & DF_SEPARATED) addN(snprintf(buf + n, sizeof(buf) - n, "%s ", L("SEPARATED TAG!", "ЧУЖАЯ МЕТКА!")));
  if (e.model[0]) addN(snprintf(buf + n, sizeof(buf) - n, "%s ", e.model));
  if (e.batL <= 100 || e.batR <= 100 || e.batC <= 100) {
    if (e.batL <= 100) addN(snprintf(buf + n, sizeof(buf) - n, "L%d ", e.batL));
    if (e.batR <= 100) addN(snprintf(buf + n, sizeof(buf) - n, "R%d ", e.batR));
    if (e.batC <= 100) addN(snprintf(buf + n, sizeof(buf) - n, "C%d ", e.batC));
  } else if (e.bat <= 100) {
    addN(snprintf(buf + n, sizeof(buf) - n, "BAT %d%% ", e.bat));
  }
  if (e.state[0] && !(e.flags & DF_SEPARATED)) addN(snprintf(buf + n, sizeof(buf) - n, "%s ", e.state));
  if (e.flags & DF_ACTIVE) addN(snprintf(buf + n, sizeof(buf) - n, "%s ", L("ACT", "АКТ")));
  if (e.flags & DF_MOVING) addN(snprintf(buf + n, sizeof(buf) - n, "%s ", L("MOV", "ДВИЖ")));
  if (e.txPwr != 127) addN(snprintf(buf + n, sizeof(buf) - n, "TX%+d", e.txPwr));

  if (buf[0]) {
    upperCopy(buf, bu, sizeof(bu));
    fitText(bu, tmp, sizeof(tmp), maxW, 1);
    spr.setTextColor((e.flags & DF_SEPARATED) ? C_ALERT() : C_ACC());
    txt(tmp, x0, y, 1); y += 10;
  }
  macStr(d.mac, tmp);
  snprintf(buf, sizeof(buf), "MAC %s  %s", tmp, akindName(d.akind));
  upperCopy(buf, bu, sizeof(bu));
  fitText(bu, tmp, sizeof(tmp), maxW, 1);
  spr.setTextColor(UI_DIM());
  txt(tmp, x0, y, 1); y += 10;
  if (d.nU) {
    n = 0; buf[0] = 0; addN(snprintf(buf, sizeof(buf), "SVC"));
    for (uint8_t i = 0; i < d.nU && n < 46; i++) {
      const char* nm = uuidName(d.uuids[i]);
      if (!nm[0]) { const char* o = btUuidOwner(d.uuids[i]); if (o) nm = o; }
      addN(snprintf(buf + n, sizeof(buf) - (size_t)n, nm[0] ? " %04X %s" : " %04X", d.uuids[i], nm));
    }
    upperCopy(buf, bu, sizeof(bu));
    fitText(bu, tmp, sizeof(tmp), maxW, 1);
    txt(tmp, x0, y, 1);
  } else if (d.hasCid) {
    n = 0; buf[0] = 0; addN(snprintf(buf, sizeof(buf), "MFR %02X %02X |", d.cid & 0xFF, d.cid >> 8));
    int shown = d.mfrLen < 6 ? d.mfrLen : 6;
    for (int i = 0; i < shown; i++) addN(snprintf(buf + n, sizeof(buf) - (size_t)n, " %02X", d.mfr[i]));
    addN(snprintf(buf + n, sizeof(buf) - (size_t)n, "%s  LEN %d", d.mfrLen > 6 ? " .." : "", d.mfrLen));
    fitText(buf, tmp, sizeof(tmp), maxW, 1);
    txt(tmp, x0, y, 1);
  }
  present();
}

// ============================================================
//  WEATHER — BLE thermometers around
// ============================================================
void drawWeather(const std::vector<Sensor>& ss, uint32_t now) {
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  snprintf(buf, sizeof(buf), "%d %s", (int)ss.size(), PL((int)ss.size(), "sensor", "sensors", "датчик", "датчика", "датчиков"));
  uiHeader(L("Weather", "Погода"), ss.empty() ? "" : buf);
  if (ss.empty()) {
    uiEmpty(L("no sensors yet", "датчиков пока нет"), "Xiaomi pvvx/atc, BTHome, Govee, Qingping, SwitchBot, Ruuvi");
    uiFooter(L("wheel: screens", "колесо: экраны"), L("side: back", "кнопка: назад"));
    present();
    return;
  }
  const int PER = 3, RH = 42;
  int pages = ((int)ss.size() + PER - 1) / PER;
  int page = pages > 1 ? (int)((now / 4000) % pages) : 0;
  for (int i = 0; i < PER; i++) {
    int k = page * PER + i;
    if (k >= (int)ss.size()) break;
    const Sensor& S = ss[k];
    int y = UI_TOP + 2 + i * RH;
    if (i > 0) spr.drawFastHLine(UI_M + 8, y - 3, W - 2 * UI_M - 16, UI_LINE());
    // label: name or model + MAC tail; right: battery and "when"
    if (S.name[0]) fitText(S.name, buf, sizeof(buf), 160, 1);
    else snprintf(buf, sizeof(buf), "%s %02X%02X", S.r.model, S.mac[1], S.mac[0]);
    char bu[48]; upperCopy(buf, bu, sizeof(bu));
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(UI_LABEL());
    txt(bu, UI_M + 8, y, 1);
    uint32_t ago = msSince(now, S.t) / 1000;
    int n = 0;
    if (S.r.hasB) n += snprintf(buf + n, sizeof(buf) - n, L("BAT %d%%  ", "БАТ %d%%  "), S.r.batt);
    if (ago < 60) snprintf(buf + n, sizeof(buf) - n, L("%luS AGO", "%lu С НАЗАД"), (unsigned long)ago);
    else          snprintf(buf + n, sizeof(buf) - n, L("%luM AGO", "%lu МИН НАЗАД"), (unsigned long)(ago / 60));
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor((S.r.hasB && S.r.batt <= 15) ? C_ALERT() : UI_DIM());
    txt(buf, W - UI_M - 8, y, 1);
    int vy = y + 11;
    if (S.r.hasT) {
      snprintf(buf, sizeof(buf), "%.1f", S.r.temp);
      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(C_HOT());
      txt(buf, UI_M + 8, vy, 4);
      int x = UI_M + 8 + txtW(buf, 4) + 3;
      spr.drawCircle(x + 2, vy + 4, 2, C_HOT());
      txt("C", x + 7, vy, 4);
    }
    if (S.r.hasH) {
      snprintf(buf, sizeof(buf), "%.0f%%", S.r.hum);
      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(C_ACC());
      txt(buf, 120, vy, 4);
    }
    int ex = 200;
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(UI_LABEL());
    if (S.r.hasP)        { snprintf(buf, sizeof(buf), L("%.0f hPa", "%.0f гПа"), S.r.press); txt(buf, ex, vy + 5, 2); }
    else if (S.r.hasCO2) { snprintf(buf, sizeof(buf), "%d ppm", S.r.co2); txt(buf, ex, vy + 5, 2); }
    else if (S.r.hasLeak && S.r.leak) { spr.setTextColor(C_ALERT()); txt(L("LEAK!", "ПРОТЕЧКА!"), ex, vy + 5, 2); }
    else if (S.r.hasDoor) { spr.setTextColor(S.r.doorOpen ? C_ALERT() : C_ACC()); txt(S.r.doorOpen ? L("OPEN", "ОТКР") : L("CLOSED", "ЗАКР"), ex, vy + 5, 2); }
    else if (S.r.hasMotion) { spr.setTextColor(S.r.motion ? C_ALERT() : UI_DIM()); txt(S.r.motion ? L("MOTION", "ДВИЖ") : L("STILL", "ТИХО"), ex, vy + 5, 2); }
    else if (S.r.hasLux) { snprintf(buf, sizeof(buf), "%.0f lx", S.r.lux); txt(buf, ex, vy + 5, 2); }
  }
  if (pages > 1) {                                   // sensor pages: number in the header
    snprintf(buf, sizeof(buf), L("%d sensors  ·  %d/%d", "%d датч.  ·  %d/%d"), (int)ss.size(), page + 1, pages);
    uiHeader(L("Weather", "Погода"), buf);
  }
  uiFooter(L("wheel: screens", "колесо: экраны"), L("side: back", "кнопка: назад"));
  present();
}
