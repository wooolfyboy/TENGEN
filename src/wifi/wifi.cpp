// ============================================================
//  wifi.cpp — Wi-Fi section. Receive-only: passive scan (we only listen to beacons, never transmit)
//  and packet counting. Payloads are not read, only headers (who talks to whom), for counting.
//  ESP32-S3 is 2.4 GHz only, so 5 GHz networks are invisible.
//  Screens: NETWORKS (+details), CHANNELS (load + advice), ACTIVITY (pps + noise),
//  CLIENTS (devices per network), FIND (hot/cold on one network, fast single-channel scan).
// ============================================================
#include <WiFi.h>
#include "core/lang.h"
#include <esp_wifi.h>
#include <map>
#include <vector>
#include <algorithm>
#include <atomic>
#include "core/timeutil.h"
#include "core/settings.h"
#include "wifi/wifi.h"
#include "core/board.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/leds.h"
#include "core/menu.h"
#include "ble/ble.h"

#include "wifi/wifi_model.h"
static const int WIFI_STEADY_DB = 3;


bool g_wOn = false;
inline bool wNeedsProm(int m) { return m == W_ACTIVITY || m == W_CLIENTS; }

// ---------- access points ----------
static const uint32_t AP_REMEMBER_MS = 60000;

uint32_t g_scanRetryMs = 0;
int      g_scanCh = 0;           // 0 = all channels, else one (FIND mode)

// list
int      g_wMove = 0;
uint32_t g_wTouch = 0;
uint64_t g_wSparkFor = 0;
// FIND
uint64_t g_fFor = 0;

// ---------- packet capture (promiscuous) ----------
// Counters are written by the packet handler (another task) and read by the main loop.
std::atomic<uint32_t> g_pMgmt{0}, g_pData{0};
std::atomic<int32_t>  g_nfSum{0};
std::atomic<uint32_t> g_nfCnt{0};
uint32_t g_hopMs = 0, g_lastMgmt = 0, g_lastData = 0;
uint32_t g_actHistMs = 0;
static const uint32_t HOP_MS = 200;

// handler -> main loop events (ring buffer)
struct PEv { uint8_t kind; uint8_t a[6]; uint8_t b[6]; };   // 1: data (a=device, b=network), 2: probe (a=source)
#define PEV_N 128
PEv g_pev[PEV_N];
std::atomic<uint16_t> g_pevHead{0}, g_pevTail{0};

// devices in networks and "looking for Wi-Fi" (RAM only, count only)
static const uint32_t STA_REMEMBER_MS = 120000;
static const size_t   STA_MAX = 500;

static inline void IRAM_ATTR pushEv(uint8_t kind, const uint8_t* a, const uint8_t* b) {
  uint16_t h = g_pevHead.load(std::memory_order_relaxed), n = (h + 1) % PEV_N;
  if (n == g_pevTail.load(std::memory_order_acquire)) return;   // buffer full: drop
  PEv& e = g_pev[h];
  e.kind = kind;
  for (int i = 0; i < 6; i++) { e.a[i] = a[i]; e.b[i] = b ? b[i] : 0; }
  g_pevHead.store(n, std::memory_order_release);
}

static void IRAM_ATTR promCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
  int nf = p->rx_ctrl.noise_floor;
  if (nf < -20 && nf > -120) { g_nfSum += nf; g_nfCnt++; }
  const uint8_t* f = p->payload;
  int len = p->rx_ctrl.sig_len;
  if (type == WIFI_PKT_MGMT) {
    g_pMgmt++;
    if (len >= 24 && (f[0] & 0xFC) == 0x40) pushEv(2, f + 10, nullptr);   // probe request
  } else if (type == WIFI_PKT_DATA) {
    g_pData++;
    if (len >= 24) {
      uint8_t ds = f[1] & 0x03;                                            // ToDS / FromDS
      if (ds == 1)      pushEv(1, f + 10, f + 4);   // device -> network
      else if (ds == 2) pushEv(1, f + 4,  f + 10);  // network -> device
    }
  }
}



// phone hotspot? (by SSID and vendor)
static bool looksLikeHotspot(const char* ssid, const char* vendor) {
  char low[33]; size_t i = 0;
  for (; i < 32 && ssid[i]; i++) low[i] = (char)tolower((unsigned char)ssid[i]);
  low[i] = 0;
  static const char* const kw[] = { "iphone", "ipad", "androidap", "android", "galaxy", "redmi",
    "poco", "honor", "realme", "oneplus", "pixel", "hotspot", "huawei p", "huawei mate",
    "mi phone", "xiaomi 1", "nothing phone", "moto ", nullptr };
  for (int k = 0; kw[k]; k++) if (strstr(low, kw[k])) return true;
  if (vendor && (!strncmp(vendor, "Apple", 5) || !strncmp(vendor, "Samsung", 7) ||
                 !strncmp(vendor, "Xiaomi Comm", 11) || !strncmp(vendor, "Motorola Mob", 12) ||
                 !strncmp(vendor, "OnePlus", 7))) return true;
  return false;
}

// ---------- radio control ----------
static void promOn() {
  if (g_prom || !g_wOn) return;
  esp_wifi_scan_stop();
  WiFi.scanDelete();
  g_scanning = false;
  wifi_promiscuous_filter_t f = { WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
  esp_wifi_set_promiscuous_filter(&f);
  esp_wifi_set_promiscuous_rx_cb(promCb);
  esp_wifi_set_promiscuous(true);
  g_hopCh = 1;
  esp_wifi_set_channel(g_hopCh, WIFI_SECOND_CHAN_NONE);
  g_hopMs = millis();
  g_lastMgmt = g_pMgmt; g_lastData = g_pData;
  g_nfSum = 0; g_nfCnt = 0;
  g_prom = true;
}
static void promOff() {
  if (!g_prom) return;
  esp_wifi_set_promiscuous(false);
  g_prom = false;
}

static void wifiStart() {
  if (g_wOn) return;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  g_wOn = true;
  g_scanning = false;
  g_scanRetryMs = 0;
}
static void wifiStop() {
  if (!g_wOn) return;
  promOff();
  esp_wifi_scan_stop();
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  g_wOn = false;
  g_scanning = false;
}
static void wifiEnter() {
  bleRadioOff();        // one antenna shared by both
  wifiStart();          // submenu only scans; packet capture starts when a screen opens
}
static void wifiLeave() {
  promOff();
  wifiRadioOff();       // left: Wi-Fi off
  ledsOff();
}
void wifiRadioOff() { wifiStop(); }

static void wifiSetMode(int m) {
  int prev = g_wMode;
  g_wMode = (m + W_NUM) % W_NUM;
  g_wDetail = false;
  if (wNeedsProm(g_wMode)) promOn(); else promOff();
  // all-channel and single-channel scans are incompatible: restart
  if ((prev == W_FIND) != (g_wMode == W_FIND) && g_scanning) {
    esp_wifi_scan_stop(); WiFi.scanDelete(); g_scanning = false;
  }
  ledsOff();
}


// ---------- scan: collect results, start the next one ----------
static void wifiOrderUpdate(bool force);

static void wifiScanTick(uint32_t now) {
  if (!g_wOn || g_prom) return;
  if (!g_scanning) {
    if ((int32_t)(now - g_scanRetryMs) < 0) return;
    g_scanCh = 0;
    uint32_t perCh = 150;
    if (g_wMode == W_FIND) {
      uint64_t t = findTarget();
      if (t) { g_scanCh = g_aps[t].ch; perCh = 110; }
    }
    // async, show_hidden, passive (nothing is sent), ms per channel, channel (0 = all)
    if (WiFi.scanNetworks(true, true, true, perCh, g_scanCh) == WIFI_SCAN_FAILED) {
      g_scanRetryMs = now + 1000; return;
    }
    g_scanning = true;
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  g_scanning = false;
  if (n < 0) { g_scanRetryMs = now + 500; return; }
  for (int i = 0; i < n; i++) {
    wifi_ap_record_t* r = (wifi_ap_record_t*)WiFi.getScanInfoByIndex(i);
    if (!r) continue;
    uint64_t k = macKey(r->bssid);
    auto it = g_aps.find(k);
    if (it == g_aps.end()) {
      static const size_t AP_MAX = 250;
      if (g_aps.size() >= AP_MAX) {
        auto oldest = g_aps.begin();
        for (auto j = g_aps.begin(); j != g_aps.end(); ++j)
          if (msSince(now, j->second.t) > msSince(now, oldest->second.t)) oldest = j;
        g_aps.erase(oldest);
      }
      Ap a; memset(&a, 0, sizeof(a));
      memcpy(a.bssid, r->bssid, 6);
      a.first = now; a.rssi = a.rssiS = a.rmin = a.rmax = r->rssi;
      a.vendor = ouiName(r->bssid);
      it = g_aps.insert({ k, a }).first;
    }
    Ap& a = it->second;
    strncpy(a.ssid, (const char*)r->ssid, 32); a.ssid[32] = 0;
    a.ch  = r->primary;
    a.enc = (uint8_t)r->authmode;
    a.sec = r->second == WIFI_SECOND_CHAN_ABOVE ? 1 : r->second == WIFI_SECOND_CHAN_BELOW ? -1 : 0;
    a.gen = r->phy_11ax ? 6 : r->phy_11n ? 4 : r->phy_11g ? 3 : 1;
    a.hotspot = looksLikeHotspot(a.ssid, a.vendor);
    a.rssiS = (int8_t)((a.rssiS + r->rssi) / 2);
    // display a steadied value: hide +-2 dB jitter
    if (abs(a.rssiS - a.rssi) >= WIFI_STEADY_DB) a.rssi = a.rssiS;
    if (r->rssi < a.rmin) a.rmin = r->rssi;
    if (r->rssi > a.rmax) a.rmax = r->rssi;
    a.t = now;
  }
  WiFi.scanDelete();

  if (g_scanCh == 0) {                       // full pass
    g_wEverScanned = true;
    for (auto it = g_aps.begin(); it != g_aps.end(); )
      if (msSince(now, it->second.t) > AP_REMEMBER_MS) it = g_aps.erase(it); else ++it;
    if (g_wDetail && g_wSel) {               // history for the detail view
      if (g_wSparkFor != g_wSel) { g_wSparkFor = g_wSel; g_wSparkN = 0; }
      auto it = g_aps.find(g_wSel);
      int8_t v = (it != g_aps.end() && it->second.t == now) ? it->second.rssi : (int8_t)-127;
      if (g_wSparkN < W_SPARK_N) g_wSpark[g_wSparkN++] = v;
      else { memmove(g_wSpark, g_wSpark + 1, W_SPARK_N - 1); g_wSpark[W_SPARK_N - 1] = v; }
    }
    wifiOrderUpdate(false);
  } else {                                    // FIND: fast single-channel pass
    uint64_t t = g_wSel;
    if (g_fFor != t) { g_fFor = t; g_fSparkN = 0; }
    auto it = g_aps.find(t);
    bool hit = it != g_aps.end() && it->second.t == now;
    if (hit) g_fLastHit = now;
    int8_t v = hit ? it->second.rssi : (int8_t)-127;
    if (g_fSparkN < F_SPARK_N) g_fSpark[g_fSparkN++] = v;
    else { memmove(g_fSpark, g_fSpark + 1, F_SPARK_N - 1); g_fSpark[F_SPARK_N - 1] = v; }
  }
}

// ---------- capture: hop channels, count packets, noise and devices ----------
static void wifiHopTick(uint32_t now) {
  if (!g_prom) return;
  // drain handler events (acquire/release safe across Core 0 -> Core 1)
  uint16_t tail = g_pevTail.load(std::memory_order_relaxed);
  while (tail != g_pevHead.load(std::memory_order_acquire)) {
    PEv e = g_pev[tail];
    tail = (tail + 1) % PEV_N;
    g_pevTail.store(tail, std::memory_order_release);
    if (e.a[0] & 0x01) continue;                       // group addresses are not devices
    uint64_t a = macKey(e.a);
    if (e.kind == 1) {
      uint64_t b = macKey(e.b);
      if (a == b) continue;
      auto it = g_stas.find(a);
      if (it != g_stas.end()) it->second = { b, now };
      else if (g_stas.size() < STA_MAX) g_stas[a] = { b, now };
    } else {
      auto it = g_probes.find(a);
      if (it != g_probes.end()) it->second = now;
      else if (g_probes.size() < STA_MAX) g_probes[a] = now;
    }
  }

  uint32_t dt = msSince(now, g_hopMs);
  if (dt < HOP_MS) return;
  uint32_t m = g_pMgmt, d = g_pData;
  float sm = (m - g_lastMgmt) * 1000.0f / dt, sd = (d - g_lastData) * 1000.0f / dt;
  g_lastMgmt = m; g_lastData = d;
  g_ppsMgmt[g_hopCh] = g_ppsMgmt[g_hopCh] * 0.4f + sm * 0.6f;
  g_ppsData[g_hopCh] = g_ppsData[g_hopCh] * 0.4f + sd * 0.6f;
  int32_t nfs = g_nfSum.exchange(0);
  uint32_t nfc = g_nfCnt.exchange(0);
  if (nfc > 0) {
    float nf = (float)nfs / nfc;
    g_noise[g_hopCh] = g_noise[g_hopCh] == 0 ? nf : g_noise[g_hopCh] * 0.5f + nf * 0.5f;
  }
  g_hopCh = g_hopCh % CH_MAX + 1;
  esp_wifi_set_channel(g_hopCh, WIFI_SECOND_CHAN_NONE);
  g_hopMs = now;

  if (msSince(now, g_actHistMs) >= 1000) {
    g_actHistMs = now;
    float tot = 0;
    for (int c = 1; c <= CH_MAX; c++) tot += g_ppsMgmt[c] + g_ppsData[c];
    if (g_actHistN < ACT_HIST_N) g_actHist[g_actHistN++] = tot;
    else { memmove(g_actHist, g_actHist + 1, sizeof(float) * (ACT_HIST_N - 1)); g_actHist[ACT_HIST_N - 1] = tot; }
    for (auto it = g_stas.begin(); it != g_stas.end(); )
      if (msSince(now, it->second.t) > STA_REMEMBER_MS) it = g_stas.erase(it); else ++it;
    for (auto it = g_probes.begin(); it != g_probes.end(); )
      if (msSince(now, it->second) > STA_REMEMBER_MS) it = g_probes.erase(it); else ++it;
  }
}


// ---------- network list: "sticky" order, like Bluetooth ----------
static void wifiOrderUpdate(bool force) {
  bool frozen = g_wDetail || (millis() - g_wTouch < 4000);
  if (frozen && !force && !g_wOrder.empty()) return;
  struct Item { uint64_t k; int8_t rs; };
  static std::vector<Item> o;
  o.clear(); o.reserve(g_aps.size());
  int minDbm = RANGES[g_wRange].dbm;               // Wi-Fi > Range setting
  for (uint64_t k : g_wOrder) {
    auto it = g_aps.find(k);
    if (it != g_aps.end() && (it->second.rssiS >= minDbm || k == g_wSel))
      o.push_back({ k, it->second.rssiS });
  }
  for (size_t pass = 0; pass < o.size(); pass++) {
    bool sw = false;
    for (size_t i = 0; i + 1 < o.size(); i++)
      if (o[i + 1].rs > o[i].rs + 6) { std::swap(o[i], o[i + 1]); sw = true; }
    if (!sw) break;
  }
  for (auto& kv : g_aps) {
    uint64_t k = kv.first;
    int8_t r = kv.second.rssiS;
    if (r < minDbm && k != g_wSel) continue;
    bool seen = false;
    for (const auto& x : o) if (x.k == k) { seen = true; break; }
    if (seen) continue;
    size_t pos = 0;
    while (pos < o.size() && o[pos].rs >= r) pos++;
    o.insert(o.begin() + pos, { k, r });
  }
  g_wOrder.resize(o.size());
  for (size_t i = 0; i < o.size(); i++) g_wOrder[i] = o[i].k;
}

static void wifiApplyMove() {
  int n = (int)g_wOrder.size();
  int idx = -1;
  for (int i = 0; i < n; i++) if (g_wOrder[i] == g_wSel) { idx = i; break; }
  int mv = g_wMove; g_wMove = 0;
  if (n == 0) return;
  if (idx < 0) { if (g_wDetail && mv == 0) return; idx = 0; }
  idx = constrain(idx + mv, 0, n - 1);
  g_wSel = g_wOrder[idx];
  if (idx < g_wTop) g_wTop = idx;
  if (idx >= g_wTop + LIST_ROWS) g_wTop = idx - LIST_ROWS + 1;
  if (g_wTop > n - LIST_ROWS) g_wTop = n - LIST_ROWS;
  if (g_wTop < 0) g_wTop = 0;
}

// wheel and click in the Wi-Fi section
static void wifiRotate(int dir) {
  static uint32_t last = 0;
  if (g_wMode == W_NETS) { g_wMove += dir; g_wTouch = millis(); return; }
  if (millis() - last < 120) return;          // do not skip screens on fast spin
  last = millis();
  wifiSetMode(g_wMode + dir);
}
static void wifiClick() {
  if (g_wMode == W_NETS && !g_wOrder.empty()) { g_wDetail = !g_wDetail; g_wTouch = millis(); return; }
  if (g_wMode == W_FIND && !g_wOrder.empty()) {        // next target network
    int idx = -1;
    for (size_t i = 0; i < g_wOrder.size(); i++) if (g_wOrder[i] == g_wSel) idx = (int)i;
    g_wSel = g_wOrder[(idx + 1) % g_wOrder.size()];
    if (g_scanning) { esp_wifi_scan_stop(); WiFi.scanDelete(); g_scanning = false; }
    return;
  }
  // other screens: click does nothing (back = side button)
}

// ---------- submenu ----------
static void wifiOpenScreen(int m) { wifiSetMode(m); }
static void wifiCloseScreen() {
  g_wDetail = false;
  promOff();
  ledsOff();
  if (g_scanCh != 0 && g_scanning) { esp_wifi_scan_stop(); WiFi.scanDelete(); g_scanning = false; }
}
static int  wifiScreenNow() { return g_wMode; }
static bool wifiCloseDetail() {
  if (g_wMode == W_NETS && g_wDetail) { g_wDetail = false; return true; }
  return false;
}



static void wifiScreenInfo(int i, SubItem& it) {
  const char* const names[W_NUM] = { L("Networks", "Сети"), L("Channels", "Каналы"), L("Activity", "Активность"), L("Clients", "Клиенты"), L("Finder", "Поиск") };
  const char* const descs[W_NUM] = { L("list + details", "список и подробности"), L("who uses which channel", "кто на каком канале"), L("packets & noise", "пакеты и шум"),
    L("devices in networks", "устройства в сетях"), L("best signal spot", "где сигнал лучше") };
  it.name = names[i]; it.desc = descs[i]; strcpy(it.value, "-");
  switch (i) {
    case W_NETS:
      if (g_wEverScanned) sprintf(it.value, "%d", (int)g_aps.size()); else strcpy(it.value, "...");
      break;
    case W_CHANNELS: { int bc = bestChannel(); if (bc) sprintf(it.value, L("best %d", "лучший %d"), bc); break; }
    case W_ACTIVITY: {
      float tot = 0;
      for (int c = 1; c <= CH_MAX; c++) tot += g_ppsMgmt[c] + g_ppsData[c];
      if (tot > 0) sprintf(it.value, "%d/s", (int)tot);
      break;
    }
    case W_CLIENTS: if (!g_stas.empty()) sprintf(it.value, "%d", (int)g_stas.size()); break;
    case W_FIND:
      if (g_wSel && g_aps.count(g_wSel)) {
        char nm[24]; fitText(ssidOr(g_aps[g_wSel]), nm, sizeof(nm), 90, 2);
        strncpy(it.value, nm, 23); it.value[23] = 0;
      }
      break;
  }
}

// every loop() pass while the section is open
static void wifiTick(uint32_t now) {
  if (navInSub()) {
    // submenu: plain all-channel scan keeps the numbers live (no FIND here)
    int saved = g_wMode;
    if (g_wMode == W_FIND) g_wMode = W_NETS;
    wifiScanTick(now);
    g_wMode = saved;
    return;
  }
  wifiScanTick(now);
  wifiHopTick(now);
}

static void wifiDraw(uint32_t now) {
  if (g_wMode == W_NETS) {
    if (g_wOrder.empty() && !g_aps.empty()) wifiOrderUpdate(true);
    wifiApplyMove();
    if (g_wDetail) drawNetDetail(now); else drawNets(now);
  }
  else if (g_wMode == W_CHANNELS) drawChannels(now);
  else if (g_wMode == W_ACTIVITY) drawActivity(now);
  else if (g_wMode == W_CLIENTS)  drawClients(now);
  else                            drawFind(now);
}

static void wifiSubtitle(char* out, size_t n) {
  if (g_wEverScanned) snprintf(out, n, "%d %s", (int)g_aps.size(), PL((int)g_aps.size(), "network", "networks", "сеть", "сети", "сетей"));
  else                snprintf(out, n, "%s", L("open to scan", "откройте для поиска"));
}

const Section wifiSection = {
  "Wi-Fi", 70, 210, 130, iconWifi, wifiSubtitle,
  wifiEnter, wifiLeave,
  W_NUM, wifiScreenInfo, wifiScreenNow, wifiOpenScreen, wifiCloseScreen,
  wifiClick, wifiRotate, wifiCloseDetail, wifiTick, wifiDraw,
};
