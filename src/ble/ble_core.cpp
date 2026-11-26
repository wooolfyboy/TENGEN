// ============================================================
//  ble_core.cpp — Bluetooth section: scanning, address merging, grouping into
//  "physical" devices, history, list, navigation. Receive only: listens to
//  advertisements, never connects.
// ============================================================
#include <NimBLEDevice.h>
#include "core/lang.h"
#include <algorithm>
#include "core/jsonutil.h"
#include "core/timeutil.h"
#include "ble/ble.h"
#include "ble/ble_model.h"
#include "ble/vendors.h"
#include "core/display.h"
#include "core/leds.h"
#include "core/menu.h"
#include "core/settings.h"
#include "wifi/wifi.h"
#include <Preferences.h>

// ---------- raw data (written by the scanner callback, under mutex) ----------
static std::map<uint64_t, Dev>    g_seen;     // by MAC address
static std::map<uint64_t, Sensor> g_sens;     // weather sensors
static uint32_t g_nextEnt = 0;
static SemaphoreHandle_t g_lock;

// ---------- section state ----------
volatile int g_mode = 0;
bool  g_bleActive = true;
int   g_lastCount = 0;
int   g_lastRaw = 0;
int   g_sensCount = 0;

Sample g_hist[HIST_N];
int    g_histLen = 0;
static float    g_base = -1.0f;
static uint32_t g_lastSampleMs = 0;
static uint32_t g_cooldownMs = 0;

std::vector<uint32_t> g_order;
static uint32_t g_orderMs = 0;
uint32_t g_sel = 0;
int      g_top = 0;
static int      g_listMove = 0;
bool     g_detail = false;
static uint32_t g_lastTouch = 0;
int8_t   g_spark[SPARK_N];
int      g_sparkN = 0;
static uint32_t g_sparkEnt = 0, g_sparkMs = 0;

// ---------- snapshot for the current frame (bleUpdate) ----------
static std::map<uint32_t, Ent> g_ents;        // physical devices
static std::vector<Sensor> g_sensSnap;
static std::vector<Blip>   g_snap;
static int   g_typeCnt[T_NUM];
static Ent   g_bestEnt;
static int   g_bestRssi = -127;
static bool  g_have = false;
static Dev   g_detDev;                        // last known details of the selected one
static bool  g_detValid = false;
static uint32_t g_detFor = 0;

static bool bleIs(int m) { return navActive(&bleSection) && g_mode == m; }

// copy parsed advertisement fields into the address record
static void fillAdv(Dev& v, const AdvInfo& ai) {
  if (ai.nameLen && !v.name[0]) {
    strncpy(v.name, ai.name, sizeof(v.name) - 1); v.name[sizeof(v.name) - 1] = 0;
  }
  if (ai.hasMfr) {
    v.hasCid = true; v.cid = ai.cid; v.mfrLen = ai.mfrLen;
    memcpy(v.mfr, ai.mfr, sizeof(v.mfr));
  }
  if (ai.hasApp) v.appearance = ai.appearance;
  for (uint8_t i = 0; i < ai.nUuid; i++) {
    bool have = false;
    for (uint8_t k = 0; k < v.nU; k++) if (v.uuids[k] == ai.uuids[i]) have = true;
    if (!have && v.nU < 4) v.uuids[v.nU++] = ai.uuids[i];
  }
}

// ---------- scanner callback (NimBLE 2.x) ----------
class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    const NimBLEAddress& addr = d->getAddress();
    uint64_t key = (uint64_t)addr | ((uint64_t)addr.getType() << 48);
    int rssi = d->getRSSI();
    const uint8_t* mac = addr.getVal();

    AdvInfo ai;
    const std::vector<uint8_t>& pl = d->getPayload();
    parseAdv(pl.data(), pl.size(), ai);
    SensorReading sr;
    bool isSensor = parseSensor(pl.data(), pl.size(), sr);
    Vend     vend = resolveVendor(ai);
    uint8_t  type = classify(ai);
    if (isSensor) type = T_SENSOR;
    uint32_t sig  = advSignature(ai);
    uint8_t akind = A_PUBLIC;
    if (addr.getType() == BLE_ADDR_RANDOM) {
      switch (mac[5] >> 6) {          // top two address bits
        case 3:  akind = A_STATIC;   break;
        case 1:  akind = A_ROTATING; break;
        default: akind = A_NONRES;   break;
      }
    }
    bool rnd = (akind == A_ROTATING || akind == A_NONRES);

    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;
    uint32_t now = millis();
    auto it = g_seen.find(key);
    if (it != g_seen.end()) {
      Dev& v = it->second;
      v.t = now; v.rssi = rssi; v.pkts++;
      v.rssiS = (int8_t)((v.rssiS * 3 + rssi) / 4);
      if (rssi < v.rmin) v.rmin = rssi;
      if (rssi > v.rmax) v.rmax = rssi;
      if (vend.score > v.vscore) { v.cat = vend.cat; v.vkind = vend.kind; v.vcode = vend.code; v.vscore = vend.score; }
      if (type != T_UNKNOWN) v.type = type;
      if (!v.sig) v.sig = sig;
      fillAdv(v, ai);
    } else if (g_seen.size() < MAX_DEVS) {
      Dev nd; memset(&nd, 0, sizeof(nd));
      nd.t = now; nd.first = now; nd.sig = sig; nd.pkts = 1;
      nd.rssi = nd.rssiS = nd.rmin = nd.rmax = (int8_t)rssi;
      nd.cat = vend.cat; nd.vkind = vend.kind; nd.vcode = vend.code; nd.vscore = vend.score;
      nd.type = type; nd.akind = akind; nd.rnd = rnd;
      memcpy(nd.mac, mac, 6);
      fillAdv(nd, ai);
      // look for a "predecessor": same fingerprint, recently went silent
      if (rnd && sig) {
        Dev* best = nullptr; int bestDiff = MERGE_DB + 1;
        for (auto& kv : g_seen) {
          Dev& o = kv.second;
          if (!o.rnd || o.merged || o.sig != sig) continue;
          uint32_t age = msSince(now, o.t);
          if (age < STALE_MIN || age > STALE_MAX) continue;
          int diff = abs((int)o.rssi - rssi);
          if (diff < bestDiff) { bestDiff = diff; best = &o; }
        }
        if (best) {
          best->merged = true;
          nd.ent = best->ent;
          nd.first = best->first;
          nd.pkts += best->pkts;
          if (best->rmin < nd.rmin) nd.rmin = best->rmin;
          if (best->rmax > nd.rmax) nd.rmax = best->rmax;
          if (nd.type == T_UNKNOWN) nd.type = best->type;
          if (best->vscore > nd.vscore) {
            nd.cat = best->cat; nd.vkind = best->vkind; nd.vcode = best->vcode; nd.vscore = best->vscore;
          }
          if (!nd.name[0]) memcpy(nd.name, best->name, sizeof(nd.name));
        }
      }
      if (!nd.ent) nd.ent = ++g_nextEnt;
      g_seen[key] = nd;
    }

    // weather sensor reading
    if (isSensor) {
      auto st = g_sens.find(key);
      if (st == g_sens.end() && g_sens.size() < MAX_SENSORS) {
        Sensor ns; memset(&ns, 0, sizeof(ns));
        memcpy(ns.mac, mac, 6);
        st = g_sens.insert({ key, ns }).first;
      }
      if (st != g_sens.end()) {
        Sensor& S = st->second;
        S.t = now; S.rssi = rssi;
        if (sr.hasT)   { S.r.temp = sr.temp;   S.r.hasT = true; }
        if (sr.hasH)   { S.r.hum = sr.hum;     S.r.hasH = true; }
        if (sr.hasB)   { S.r.batt = sr.batt;   S.r.hasB = true; }
        if (sr.hasP)   { S.r.press = sr.press; S.r.hasP = true; }
        if (sr.hasCO2) { S.r.co2 = sr.co2;     S.r.hasCO2 = true; }
        S.r.model = sr.model;
        if (ai.nameLen && !S.name[0]) {
          strncpy(S.name, ai.name, sizeof(S.name) - 1); S.name[sizeof(S.name) - 1] = 0;
        }
      }
    }
    xSemaphoreGive(g_lock);
  }
};

// ---------- history sampler (once a second) + +-7 event detector ----------
void bleSample(uint32_t now) {
  if (!g_bleActive) return;                       // Bluetooth paused: no history
  if (now - g_lastSampleMs < 1000) return;
  g_lastSampleMs = now;
  int c = g_lastCount;

  if (g_base < 0) g_base = c;                     // init
  int8_t ev = 0;
  float delta = c - g_base;
  if (g_thresh > 0 && (int32_t)(now - g_cooldownMs) >= 0) {
    if (delta >= g_thresh)       { ev = +1; g_base = c; g_cooldownMs = now + 3000; }
    else if (delta <= -g_thresh) { ev = -1; g_base = c; g_cooldownMs = now + 3000; }
  }
  if (ev != 0 && navActive(&bleSection)) ledsEvent(ev);   // LED feed only inside the Bluetooth section
  g_base += 0.15f * (c - g_base);                 // baseline drift (EMA)

  Sample s = { c, g_base, ev };
  if (g_histLen < HIST_N) g_hist[g_histLen++] = s;
  else { memmove(&g_hist[0], &g_hist[1], sizeof(Sample) * (HIST_N - 1)); g_hist[HIST_N - 1] = s; }
}

void bleBegin() {
  g_lock = xSemaphoreCreateMutex();
  NimBLEDevice::init("");
  NimBLEScan* s = NimBLEDevice::getScan();
  s->setScanCallbacks(new ScanCB(), true);   // true = every packet, not just the first
  s->setActiveScan(true);
  s->setInterval(100);
  s->setWindow(99);
  s->setMaxResults(0);                        // do not accumulate results: all handled in the callback
  g_bleActive = false;                        // scanning starts only on entering the section
}

void bleRadioOn() {
  if (g_bleActive) return;
  if (!NimBLEDevice::getScan()->start(0, false)) return;
  g_bleActive = true;
  g_base = -1;                              // re-learn the event baseline
  g_cooldownMs = millis() + 10000;          // and stay quiet while the list fills
}

bool bleRadioActive() { return g_bleActive; }

// ids of devices on air now (for "radio weather": who came, who left)
int bleEntityIds(uint32_t* out, int max) {
  int n = 0;
  for (auto& kv : g_ents) { if (n >= max) break; out[n++] = kv.first; }
  return n;
}

void bleRadioOff() {
  if (!g_bleActive) return;
  NimBLEDevice::getScan()->stop();
  g_bleActive = false;
}

// ---------- "calm" signal for screens ----------
// One device's signal always jitters by +-2-3 dB; that is useless and makes
// Scope targets and City towers twitch. So:
//  - the target moves only when the smoothed signal is STEADY_DB away from it;
//  - the shown value eases to the target (~0.7 s), no jumps.
// Looks matter more than accuracy: sorting and filters still use rssiS.
struct Steady { float shown; int8_t target; uint32_t t; };
static std::map<uint32_t, Steady> g_steady;
static const int STEADY_DB = 4;
static const int BEST_HYST_DB = 3;            // "nearest" changes only if another is stronger by 3 dB
static uint32_t g_steadyMs = 0, g_bestId = 0;

static int steadyRssi(uint32_t id, int rs, uint32_t now, float k) {
  auto it = g_steady.find(id);
  if (it == g_steady.end()) { g_steady[id] = { (float)rs, (int8_t)rs, now }; return rs; }
  Steady& s = it->second;
  s.t = now;
  if (abs(rs - s.target) >= STEADY_DB) s.target = (int8_t)rs;
  s.shown += (s.target - s.shown) * k;
  return (int)lroundf(s.shown);
}

// ---------- device labels (set by the user on the tablet) ----------
// Stored per MAC in settings memory, up to LABEL_MAX. Phones rotate their address:
// a label lasts until the address changes; we report that honestly (stable=false).
#define LABEL_MAX 32
struct Label { uint8_t mac[6]; char name[21]; };
static Label g_labels[LABEL_MAX];
static int   g_nLabels = -1;                              // -1 = not loaded yet

static void labelsLoad() {
  if (g_nLabels >= 0) return;
  Preferences p; p.begin("labels", true);
  size_t n = p.getBytes("l", g_labels, sizeof(g_labels));
  p.end();
  g_nLabels = (int)(n / sizeof(Label));
}
static void labelsSave() {
  Preferences p; p.begin("labels", false);
  if (g_nLabels > 0) p.putBytes("l", g_labels, g_nLabels * sizeof(Label)); else p.remove("l");
  p.end();
}
static const char* labelFor(const uint8_t* mac) {
  for (int i = 0; i < g_nLabels; i++) if (!memcmp(g_labels[i].mac, mac, 6)) return g_labels[i].name;
  return nullptr;
}

// /api/label: label a device (id from the stream) or clear it (empty name)
bool bleLabelSet(uint32_t id, const char* name, bool& stable) {
  labelsLoad();
  auto it = g_ents.find(id);
  if (it == g_ents.end()) return false;
  uint8_t mac[6]; uint8_t akind = A_ROTATING;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  auto d = g_seen.find(it->second.key);
  bool ok = d != g_seen.end();
  if (ok) { memcpy(mac, d->second.mac, 6); akind = d->second.akind; }
  xSemaphoreGive(g_lock);
  if (!ok) return false;
  stable = akind == A_PUBLIC || akind == A_STATIC;
  int k = 0;
  while (k < g_nLabels && memcmp(g_labels[k].mac, mac, 6)) k++;
  if (!name || !name[0]) {                                  // clear
    if (k < g_nLabels) { g_labels[k] = g_labels[g_nLabels - 1]; g_nLabels--; }
  } else {
    if (k == g_nLabels) {
      if (g_nLabels >= LABEL_MAX) { memmove(g_labels, g_labels + 1, sizeof(Label) * (LABEL_MAX - 1)); k = LABEL_MAX - 1; }   // full: evict the oldest
      else g_nLabels++;
      memcpy(g_labels[k].mac, mac, 6);
    }
    size_t n = strlen(name);
    if (n > 20) {                                           // do not cut in the middle of a UTF-8 character
      n = 20;
      while (n > 0 && (name[n] & 0xC0) == 0x80) n--;
    }
    memcpy(g_labels[k].name, name, n); g_labels[k].name[n] = 0;
  }
  labelsSave();
  return true;
}

// ---------- every frame: addresses -> physical devices ----------
void bleUpdate(uint32_t now) {
  labelsLoad();
  if (g_detFor != g_sel) { g_detValid = false; g_detFor = g_sel; }
  bool wantDetail = bleIs(M_LIST) && g_detail;
  uint32_t detT = 0;
  int raw = 0;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return;   // keep previous frame on contention
  // do not recreate map nodes every frame (less heap churn): zero them, drop the missing ones below
  for (auto& kv : g_ents) memset(&kv.second, 0, sizeof(Ent));
  g_sensSnap.clear();
  g_snap.clear();
  int thr = RANGES[g_range].dbm;
  for (auto it = g_seen.begin(); it != g_seen.end(); ) {
    if (msSince(now, it->second.t) > REMEMBER_MS) { it = g_seen.erase(it); continue; }
    Dev& d = it->second;
    // range filter with hysteresis + drop single-packet devices
    if (!d.inRange && d.rssiS >= thr) d.inRange = true;
    else if (d.inRange && d.rssiS < thr - RANGE_HYST_DB) d.inRange = false;
    if (!d.inRange || d.pkts < MIN_PKTS) { ++it; continue; }
    Ent& e = g_ents[d.ent];           // a new Ent is created zeroed
    if (d.t >= e.t) { e.t = d.t; e.rssi = d.rssi; e.rssiS = d.rssiS; e.key = it->first; }
    if (!e.first || d.first < e.first) e.first = d.first;
    if (d.pkts > e.pkts) e.pkts = d.pkts;
    if (e.nMacs < 255) e.nMacs++;                // uint8_t: do not wrap to 0
    if (d.type != T_UNKNOWN) e.type = d.type;
    if (d.vscore > e.vscore) { e.cat = d.cat; e.vkind = d.vkind; e.vcode = d.vcode; e.vscore = d.vscore; }
    if (d.name[0] && !e.name[0]) memcpy(e.name, d.name, sizeof(e.name));
    if (g_nLabels > 0 && !e.label[0]) { const char* l = labelFor(d.mac); if (l) strlcpy(e.label, l, sizeof(e.label)); }
    if (wantDetail && d.ent == g_sel && d.t >= detT) { g_detDev = d; detT = d.t; g_detValid = true; }
    ++it;
  }
  raw = (int)g_seen.size();
  g_sensCount = 0;
  for (auto it = g_sens.begin(); it != g_sens.end(); ) {
    if (msSince(now, it->second.t) > SENSOR_REMEMBER_MS) { it = g_sens.erase(it); continue; }
    g_sensCount++;
    if (bleIs(M_WEATHER)) g_sensSnap.push_back(it->second);
    ++it;
  }
  xSemaphoreGive(g_lock);
  for (auto it = g_ents.begin(); it != g_ents.end(); )   // not seen this frame: gone
    if (it->second.t == 0) it = g_ents.erase(it); else ++it;

  // calm values for all screens
  float dt = g_steadyMs ? msSince(now, g_steadyMs) / 1000.0f : 0.0f;
  g_steadyMs = now;
  float k = 1.0f - expf(-dt / 0.7f);
  for (auto& kv : g_ents) kv.second.rssi = steadyRssi(kv.first, kv.second.rssiS, now, k);
  for (auto it = g_steady.begin(); it != g_steady.end(); )
    if (msSince(now, it->second.t) > REMEMBER_MS) it = g_steady.erase(it); else ++it;

  g_have = false; g_bestRssi = -127; memset(&g_bestEnt, 0, sizeof(g_bestEnt));
  memset(g_typeCnt, 0, sizeof(g_typeCnt));
  bool wantScope = bleIs(M_SCOPE) || bleIs(M_CITY);
  auto prevBest = g_ents.find(g_bestId);
  int prevR = prevBest != g_ents.end() ? prevBest->second.rssi + BEST_HYST_DB : -127;
  if (prevBest != g_ents.end()) { g_bestRssi = prevR; g_bestEnt = prevBest->second; g_have = true; }
  for (auto& kv : g_ents) {
    const Ent& e = kv.second;
    if (e.rssi > g_bestRssi) { g_bestRssi = e.rssi; g_bestEnt = e; g_bestId = kv.first; g_have = true; }
    g_typeCnt[e.type < T_NUM ? (int)e.type : (int)T_UNKNOWN]++;
    // blip angle comes from the physical device id: stable across MAC changes
    if (wantScope) { Blip b = { e.rssi, kv.first * 2654435761u, e.cat, e.type, {0} }; strlcpy(b.label, e.label, sizeof(b.label)); g_snap.push_back(b); }
  }
  if (g_have) g_bestRssi = g_bestEnt.rssi;            // drop the hysteresis head start
  g_lastCount = (int)g_ents.size();
  g_lastRaw = raw;
}

// ---------- list: "sticky" order and row selection ----------
static void listUpdate(uint32_t now) {
  // nearest on top, but rows do not jump:
  //  - while the wheel turns or details are open, the order is frozen;
  //  - neighbours swap only if the signal differs by > ORDER_HYST_DB;
  //  - new devices go to their place, departed ones are removed.
  bool frozen = g_detail || (msSince(now, g_lastTouch) < ORDER_FREEZE);
  if (g_order.empty() || (!frozen && msSince(now, g_orderMs) >= ORDER_EVERY)) {
    struct Item { uint32_t id; int8_t rs; };
    static std::vector<Item> o;
    o.clear(); o.reserve(g_ents.size());
    for (uint32_t id : g_order) {
      auto it = g_ents.find(id);
      if (it != g_ents.end()) o.push_back({ id, it->second.rssiS });
    }
    for (size_t pass = 0; pass < o.size(); pass++) {
      bool sw = false;
      for (size_t i = 0; i + 1 < o.size(); i++)
        if (o[i + 1].rs > o[i].rs + ORDER_HYST_DB) { std::swap(o[i], o[i + 1]); sw = true; }
      if (!sw) break;
    }
    for (auto& kv : g_ents) {
      uint32_t id = kv.first;
      bool seen = false;
      for (const auto& x : o) if (x.id == id) { seen = true; break; }
      if (seen) continue;
      int8_t r = kv.second.rssiS;
      size_t pos = 0;
      while (pos < o.size() && o[pos].rs >= r) pos++;
      o.insert(o.begin() + pos, { id, r });
    }
    g_order.resize(o.size());
    for (size_t i = 0; i < o.size(); i++) g_order[i] = o[i].id;
    g_orderMs = now;
  }
  int n = (int)g_order.size();
  int idx = -1;
  for (int i = 0; i < n; i++) if (g_order[i] == g_sel) { idx = i; break; }
  int mv = g_listMove; g_listMove = 0;
  bool keep = (idx < 0 && g_detail && mv == 0);   // selected one vanished: keep its details
  if (!keep && n > 0) {
    if (idx < 0) idx = 0;
    idx = constrain(idx + mv, 0, n - 1);
    g_sel = g_order[idx];
    if (idx < g_top) g_top = idx;
    if (idx >= g_top + LIST_ROWS) g_top = idx - LIST_ROWS + 1;
  }
  if (g_top > n - LIST_ROWS) g_top = n - LIST_ROWS;
  if (g_top < 0) g_top = 0;
}

static void drawListOrDetail(uint32_t now) {
  listUpdate(now);
  if (!(g_detail && g_sel)) { drawList(g_ents); return; }
  auto it = g_ents.find(g_sel);
  bool inMap = it != g_ents.end();
  // signal history of the selected one: a point every 0.5 s
  if (g_sparkEnt != g_sel) { g_sparkEnt = g_sel; g_sparkN = 0; g_sparkMs = 0; }
  if (msSince(now, g_sparkMs) >= 500) {
    g_sparkMs = now;
    int8_t v = (inMap && msSince(now, it->second.t) < 3000) ? (int8_t)it->second.rssi : (int8_t)-127;
    if (g_sparkN < SPARK_N) g_spark[g_sparkN++] = v;
    else { memmove(g_spark, g_spark + 1, SPARK_N - 1); g_spark[SPARK_N - 1] = v; }
  }
  static Ent lastEnt;
  if (inMap) lastEnt = it->second;
  if (g_detValid) drawDetail(g_detDev, lastEnt, inMap, now);
  else            drawList(g_ents);
}

// ---------- section for the menu ----------
static void bleSubtitle(char* out, size_t n) {
  if (g_bleActive) snprintf(out, n, L("%d devices", "%d в эфире"), g_lastCount);
  else             snprintf(out, n, "%s", L("off", "выкл"));
}
static void bleEnter() { wifiRadioOff(); bleRadioOn(); }
static void bleLeave() { bleRadioOff(); ledsOff(); }   // left: radio and feed off

static void bleScreenInfo(int i, SubItem& it) {
  const char* const names[NUM_MODES] = { L("Overview", "Обзор"), L("Scope", "Радар"), L("City", "Город"), L("Nearest", "Ближайшее"), L("Devices", "Устройства"), L("Weather", "Погода") };
  const char* const descs[NUM_MODES] = { L("count, trend, types", "число, тренд, типы"), L("radar picture", "картина вокруг"), L("3D: devices as towers", "3D: башни-устройства"), L("closest device", "самое близкое"),
    L("list + details", "список и подробности"), L("BLE thermometers", "BLE-термометры") };
  it.name = names[i]; it.desc = descs[i]; it.value[0] = 0;
  switch (i) {
    case M_OVERVIEW: case M_LIST: case M_CITY: sprintf(it.value, "%d", g_lastCount); break;
    case M_SCOPE: {
      int top = -1;
      for (int t = 1; t < T_NUM; t++) if (g_typeCnt[t] > 0 && (top < 0 || g_typeCnt[t] > g_typeCnt[top])) top = t;
      if (top > 0) snprintf(it.value, sizeof(it.value), "%s %d", typeName(top), g_typeCnt[top]);
      break;
    }
    case M_NEAREST:
      if (g_have) sprintf(it.value, L("%d dBm", "%d дБм"), g_bestRssi); else strcpy(it.value, "-");
      break;
    case M_WEATHER:
      if (g_sensCount) sprintf(it.value, "%d", g_sensCount); else strcpy(it.value, "-");
      break;
  }
}
static int  bleScreenNow() { return g_mode; }
static void bleOpen(int m) { g_mode = m; g_detail = false; }
static void bleClose() { g_detail = false; }

static void bleClick() {
  if (g_mode == M_LIST) { g_detail = !g_detail; g_lastTouch = millis(); }
  // other screens have no press action (back = side button)
}
static void bleRotate(int dir) {
  static uint32_t last = 0;
  if (g_mode == M_LIST) { g_listMove += dir; g_lastTouch = millis(); return; }
  if (g_mode == M_CITY) { cityTurn(dir); return; }   // in City the wheel turns the camera
  if (millis() - last < 120) return;               // so screens do not flip by on fast spinning
  last = millis();
  g_mode = (g_mode + dir + NUM_MODES) % NUM_MODES;
  g_detail = false;
}
static bool bleBack() {
  if (g_detail) { g_detail = false; return true; }
  return false;
}
static void bleDraw(uint32_t now) {
  switch (g_mode) {
    case M_OVERVIEW: drawOverview(g_lastCount, g_lastRaw, g_typeCnt); break;
    case M_SCOPE:    drawScope(g_snap); break;
    case M_CITY:     drawCity(g_snap, now); break;
    case M_NEAREST:  drawNearest(g_have, g_bestRssi, g_bestEnt, g_lastCount); break;
    case M_LIST:     drawListOrDetail(now); break;
    default:         drawWeather(g_sensSnap, now); break;
  }
}

const Section bleSection = {
  "Bluetooth", 80, 150, 240, iconBluetooth, bleSubtitle,
  bleEnter, bleLeave,
  NUM_MODES, bleScreenInfo, bleScreenNow, bleOpen, bleClose,
  bleClick, bleRotate, bleBack, nullptr, bleDraw,
};

// ---------- for the web page: devices as JSON ----------

void bleJson(String& o) {
  uint32_t now = millis();
  std::vector<std::pair<uint32_t, const Ent*>> v;
  v.reserve(g_ents.size());
  for (auto& kv : g_ents) v.push_back({ kv.first, &kv.second });
  std::sort(v.begin(), v.end(), [](const std::pair<uint32_t, const Ent*>& a, const std::pair<uint32_t, const Ent*>& b) { return a.second->rssiS > b.second->rssiS; });
  if (v.size() > 150) v.resize(150);
  o.reserve(64 + v.size() * 110);
  o += "{\"on\":"; o += g_bleActive ? "true" : "false";
  o += ",\"count\":"; o += g_lastCount;
  o += ",\"macs\":"; o += g_lastRaw;
  o += ",\"dev\":[";
  bool first = true;
  for (auto& pe : v) {
    const Ent* e = pe.second;
    if (!first) o += ',';
    first = false;
    o += "{\"id\":"; o += pe.first;
    o += ",\"n\":"; jsonStr(o, e->name);
    o += ",\"t\":"; jsonStr(o, typeName(e->type));
    o += ",\"v\":"; jsonStr(o, vendorLabel(e->cat, e->vkind, e->vcode));
    o += ",\"r\":"; o += e->rssiS;
    o += ",\"m\":"; o += e->nMacs;
    o += ",\"p\":"; o += e->pkts;
    o += ",\"age\":"; o += msSince(now, e->first) / 1000;
    o += '}';
  }
  o += "],\"sens\":[";
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    bool f = true;
    for (auto& kv : g_sens) {
      const Sensor& z = kv.second;
      if (!f) o += ',';
      f = false;
      o += "{\"n\":"; jsonStr(o, z.name[0] ? z.name : z.r.model);
      o += ",\"mo\":"; jsonStr(o, z.r.model);
      o += ",\"r\":"; o += z.rssi;
      o += ",\"ago\":"; o += msSince(now, z.t) / 1000;
      if (z.r.hasT) { o += ",\"t\":"; o += String(z.r.temp, 1); }
      if (z.r.hasH) { o += ",\"h\":"; o += String(z.r.hum, 0); }
      if (z.r.hasB) { o += ",\"b\":"; o += z.r.batt; }
      if (z.r.hasP) { o += ",\"p\":"; o += String(z.r.press, 1); }
      if (z.r.hasCO2) { o += ",\"co2\":"; o += z.r.co2; }
      o += '}';
    }
    xSemaphoreGive(g_lock);
  }
  o += "]}";
}

// details of one device: all its addresses (MACs rotate), services, manufacturer data
void bleDevJson(uint32_t id, String& o) {
  static const char* const AK[] = { "public", "static random", "rotating (private)", "non-resolvable" };
  uint32_t now = millis();
  o = "{\"addr\":[";
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    bool f = true;
    const Dev* best = nullptr;
    for (auto& kv : g_seen) {
      const Dev& d = kv.second;
      if (d.ent != id) continue;
      if (!best || d.t > best->t) best = &d;
      char mac[18]; macStr(d.mac, mac);
      if (!f) o += ',';
      f = false;
      o += "{\"mac\":\""; o += mac; o += "\",\"kind\":\""; o += AK[d.akind & 3];
      o += "\",\"r\":"; o += d.rssi; o += ",\"ago\":"; o += msSince(now, d.t) / 1000; o += '}';
    }
    o += ']';
    if (best) {
      const Dev& d = *best;
      o += ",\"n\":"; jsonStr(o, d.name);
      { auto ei = g_ents.find(id); o += ",\"label\":"; jsonStr(o, ei != g_ents.end() ? ei->second.label : ""); }
      o += ",\"t\":"; jsonStr(o, typeName(d.type));
      o += ",\"v\":"; jsonStr(o, vendorLabel(d.cat, d.vkind, d.vcode));
      o += ",\"r\":"; o += d.rssi;
      o += ",\"rmin\":"; o += d.rmin; o += ",\"rmax\":"; o += d.rmax;
      o += ",\"pk\":"; o += d.pkts;
      o += ",\"age\":"; o += msSince(now, d.first) / 1000;
      if (d.hasCid) { char b[8]; snprintf(b, sizeof(b), "0x%04X", d.cid); o += ",\"cid\":\""; o += b; o += '"'; }
      if (d.appearance) { o += ",\"app\":"; o += d.appearance; }
      o += ",\"uu\":[";
      for (int i = 0; i < d.nU; i++) {
        char b[8]; snprintf(b, sizeof(b), "%04X", d.uuids[i]);
        const char* own = btUuidOwner(d.uuids[i]);
        if (i) o += ',';
        o += "{\"u\":\""; o += b; o += "\",\"o\":"; jsonStr(o, own ? own : ""); o += '}';
      }
      o += "],\"mfr\":\"";
      for (int i = 0; i < min<int>(d.mfrLen, 6); i++) { char b[4]; snprintf(b, sizeof(b), "%02X", d.mfr[i]); o += b; }
      if (d.mfrLen > 6) o += "..";
      o += '"';
    }
    xSemaphoreGive(g_lock);
  } else o += ']';
  o += '}';
}

// web mode: Bluetooth scans less often so Wi-Fi gets enough air
void bleLowDuty(bool low) {
  NimBLEScan* s = NimBLEDevice::getScan();
  bool was = g_bleActive;
  if (was) s->stop();
  s->setWindow(low ? 45 : 99);
  if (was && !s->start(0, false)) g_bleActive = false;
}

// for the tablet (SSE stream): compact, arrays instead of objects, up to 80 devices
// {"n":count,"m":addresses,"d":[[id,rssi,"type","vendor","name",age_s,addresses,"label"],...]}
void bleStreamJson(String& o) {
  uint32_t now = millis();
  std::vector<std::pair<uint32_t, const Ent*>> v;
  v.reserve(g_ents.size());
  for (auto& kv : g_ents) v.push_back({ kv.first, &kv.second });
  std::sort(v.begin(), v.end(), [](const std::pair<uint32_t, const Ent*>& a, const std::pair<uint32_t, const Ent*>& b) { return a.second->rssiS > b.second->rssiS; });
  if (v.size() > 80) v.resize(80);
  o.reserve(48 + v.size() * 64);
  o = "{\"on\":"; o += g_bleActive ? 1 : 0;
  o += ",\"n\":"; o += g_lastCount; o += ",\"m\":"; o += g_lastRaw; o += ",\"d\":[";
  for (size_t i = 0; i < v.size(); i++) {
    const Ent* e = v[i].second;
    if (i) o += ',';
    o += '['; o += v[i].first; o += ','; o += e->rssi; o += ',';   // "calm" value
    jsonStr(o, typeName(e->type)); o += ','; jsonStr(o, vendorLabel(e->cat, e->vkind, e->vcode)); o += ',';
    jsonStr(o, e->name); o += ','; o += msSince(now, e->first) / 1000; o += ','; o += e->nMacs; o += ','; jsonStr(o, e->label); o += ']';
  }
  o += "]}";
}

bool bleMatchMac(const uint8_t mac[6], uint32_t& outEntId) {
  bool found = false;
  uint32_t bestT = 0;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(15)) == pdTRUE) {
    for (const auto& kv : g_seen) {
      const Dev& d = kv.second;
      if (memcmp(d.mac, mac, 6) == 0 && (!found || d.t >= bestT)) {
        outEntId = d.ent;
        bestT = d.t;
        found = true;
      }
    }
    xSemaphoreGive(g_lock);
  }
  return found;
}

void bleLocatorBoost(bool on) {
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // max +9 dBm RF gain/power
  NimBLEScan* s = NimBLEDevice::getScan();
  bool was = g_bleActive;
  if (was) s->stop();
  s->setActiveScan(true);
  if (on) {
    s->setInterval(64);   // 40 ms interval
    s->setWindow(52);     // 32.5 ms window (~81% BLE duty cycle while leaving room for Ch 6 ESP-NOW!)
  } else {
    s->setInterval(100);
    s->setWindow(45);
  }
  if (was && !s->start(0, false)) g_bleActive = false;
}

// For Locator 2x: iterates over ALL active physical entities in g_seen (down to -102 dBm,
// bypassing Settings > Range cutoff and 4 dB display deadband, with sub-dB precision).
void bleForEachEnt(BleEntCb cb, void* ctx) {
  labelsLoad();
  struct Agg {
    uint32_t t;
    float    rssiSum;
    float    weightSum;
    int8_t   peakRssi;
    uint8_t  cat, type, vkind, vscore;
    uint16_t vcode;
    char     name[20];
    char     label[21];
  };
  static std::map<uint32_t, Agg> locEnts;
  locEnts.clear();
  uint32_t now = millis();
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(25)) == pdTRUE) {
    for (const auto& kv : g_seen) {
      const Dev& d = kv.second;
      if (msSince(now, d.t) > 15000 || d.rssiS < -102) continue;
      Agg& a = locEnts[d.ent];
      float ageS = msSince(now, d.t) / 1000.0f;
      float w = 1.0f / (1.0f + ageS * 0.4f);
      // Blend raw rssi, smoothed rssiS, and rmax to preserve sub-dB resolution
      float rSub = d.rssiS * 0.65f + d.rssi * 0.25f + d.rmax * 0.10f;
      a.rssiSum += rSub * w;
      a.weightSum += w;
      if (a.t == 0 || d.rssi > a.peakRssi) a.peakRssi = d.rssi;
      if (d.t >= a.t) a.t = d.t;
      if (d.type != T_UNKNOWN) a.type = d.type;
      if (d.vscore > a.vscore) { a.cat = d.cat; a.vkind = d.vkind; a.vcode = d.vcode; a.vscore = d.vscore; }
      if (d.name[0] && !a.name[0]) memcpy(a.name, d.name, sizeof(a.name));
      if (g_nLabels > 0 && !a.label[0]) { const char* l = labelFor(d.mac); if (l) strlcpy(a.label, l, sizeof(a.label)); }
    }
    xSemaphoreGive(g_lock);
  }
  for (const auto& kv : locEnts) {
    const Agg& a = kv.second;
    if (a.weightSum <= 0) continue;
    float rF = a.rssiSum / a.weightSum;
    cb(kv.first, rF, a.type, typeName(a.type), vendorLabel(a.cat, a.vkind, a.vcode), a.name, a.label, ctx);
  }
}
