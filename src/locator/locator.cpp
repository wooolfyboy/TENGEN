// ============================================================
//  locator.cpp — Dedicated 2-stick BLE Triangulation / Stereo Locator
//
//  Radio isolation design:
//  - Runs ONLY when the user opens the "Locator" section on T-Embed OR
//    views "HQ > Locator 3D" on PC/Tablet (want=loc).
//  - Locks Wi-Fi to Channel 6 (pausing background 13-channel Wi-Fi scans)
//    and runs NimBLE scan + ESP-NOW receiver simultaneously with 0% conflicts.
// ============================================================
#include "locator/locator.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include "ble/ble.h"
#include "ble/devtypes.h"
#include "wifi/wifi.h"
#include "core/lang.h"
#include "core/theme.h"
#include "core/display.h"
#include "core/ui.h"
#include "core/jsonutil.h"
#include "core/timeutil.h"
#include "core/crumb.h"

#pragma pack(push, 1)
struct EarEntry {
  uint8_t mac[6];
  int8_t  rssi;
  uint8_t type;
};
struct EarPacket {
  char     magic[4];   // "TGN1"
  uint8_t  nodeId;     // 1 = Ear #2 (M5StickC Plus)
  uint8_t  batPct;     // 0..100
  uint8_t  flags;      // bit 0: charging/USB
  uint8_t  count;      // 0..28
  uint32_t seq;
  EarEntry entries[28];
};
struct AckPacket {
  char     magic[4];   // "TGNA"
  uint8_t  channel;    // 6
  uint8_t  active;     // 1
  uint16_t matched;
};
#pragma pack(pop)

struct EarRaw {
  uint8_t  mac[6];
  float    rssiS;
  uint8_t  type;
  uint32_t seenMs;
};

struct LocNode {
  uint32_t id;
  bool     has1, has2;
  uint32_t seen1Ms, seen2Ms;
  float    r1F, r2F;       // Kalman-filtered sub-decibel RSSI (r2F includes antenna offset)
  float    r1Var, r2Var;   // 1D Kalman variance
  int      r1, r2;         // rounded integer dBm for display
  float    r2RawF;
  float    targetX, targetZ; // deadband-stabilized target coordinates
  float    x, z, err;        // smoothly gliding display coordinates (meters)
  uint8_t  rawType;
  char     type[16];
  char     vendor[24];
  char     name[24];
  char     label[24];
  uint32_t seenMs;
};

static const int MAX_EAR_RAW = 48;
static const int MAX_LOC_NODES = 64;
static EarRaw    s_earRaw[MAX_EAR_RAW];
static int       s_nEarRaw = 0;
static LocNode   s_nodes[MAX_LOC_NODES];
static int       s_nNodes = 0;

static SemaphoreHandle_t s_locLock = nullptr;
static bool      s_on = false;
static bool      s_ownWifi = false;
static bool      s_espNowOk = false;
static uint8_t   s_earMac[6] = {0};
static bool      s_earPeerAdded = false;
static uint32_t  s_earSeenMs = 0;
static uint8_t   s_earBat = 0;
static uint8_t   s_earFlags = 0;
static uint32_t  s_earPkts = 0;
static int       s_earLinkRssi = -70;
static uint32_t  s_ackMs = 0;
static uint32_t  s_calcMs = 0;
static uint16_t  s_matchedCnt = 0;

// Calibration parameters persisted in NVS
static float     s_baseM = 3.0f;       // distance between Stick #1 and Stick #2 in meters
static int       s_offsetDb = 6;       // added to Ear #2 RSSI to match T-Embed antenna sensitivity
static bool      s_prefsLoaded = false;
static uint32_t  s_calToastMs = 0;
static char      s_calMsg[40] = "";

static void loadPrefs() {
  if (s_prefsLoaded) return;
  s_prefsLoaded = true;
  Preferences p;
  if (p.begin("loc", true)) {
    s_baseM = p.getFloat("base", 3.0f);
    if (!(s_baseM >= 0.5f && s_baseM <= 15.0f)) s_baseM = 3.0f;
    s_offsetDb = p.getInt("ofs", 6);
    if (s_offsetDb < -25 || s_offsetDb > 25) s_offsetDb = 6;
    p.end();
  }
}

static void savePrefs() {
  Preferences p;
  if (p.begin("loc", false)) {
    p.putFloat("base", s_baseM);
    p.putInt("ofs", s_offsetDb);
    p.end();
  }
}

// Per-device-class 1m reference RSSI (watches/trackers/earbuds transmit quieter than laptops/phones)
static float txRefForType(uint8_t rawType) {
  switch (rawType) {
    case T_COMPUTER:
    case T_TV:      return -53.5f;
    case T_PHONE:   return -58.0f;
    case T_AUDIO:   return -60.5f;
    case T_WATCH:   return -63.0f;
    case T_TRACKER: return -62.5f;
    default:        return -58.5f;
  }
}

// Convert sub-dB RSSI to indoor meters using device-class TX power & path-loss exponent n = 2.25
static float rssiToMeters(float rssi, uint8_t rawType = 0) {
  float ref = txRefForType(rawType);
  float d = powf(10.0f, (ref - rssi) / (10.0f * 2.25f));
  return constrain(d, 0.20f, 14.0f);
}

// 1D Anti-Multipath Kalman update: rejects single-channel destructive fade drops (>6 dB dip)
// while gliding smoothly with sub-dB precision.
static void kalmanRssi(float& est, float& var, float meas) {
  if (var <= 0.0f) { est = meas; var = 3.0f; return; }
  float diff = meas - est;
  float Q = 0.18f;
  float R = (diff < -6.0f) ? 28.0f : (diff < -2.0f) ? 9.0f : 3.8f;
  var += Q;
  float K = var / (var + R);
  est += K * diff;
  var = (1.0f - K) * var;
}

// ESP-NOW receive callback (runs in Wi-Fi task)
static void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!data || len < 12) return;
  if (memcmp(data, "TGN1", 4) != 0) return;
  const EarPacket* pkt = (const EarPacket*)data;
  int cnt = min<int>(pkt->count, 28);
  if (len < 12 + cnt * (int)sizeof(EarEntry)) return;

  if (!s_locLock || xSemaphoreTake(s_locLock, pdMS_TO_TICKS(10)) != pdTRUE) return;
  uint32_t now = millis();
  s_earSeenMs = now ? now : 1;
  s_earBat = pkt->batPct;
  s_earFlags = pkt->flags;
  s_earPkts++;
  if (info && info->rx_ctrl) {
    s_earLinkRssi = (s_earLinkRssi * 3 + info->rx_ctrl->rssi) / 4;
  }
  if (info && info->src_addr) {
    memcpy(s_earMac, info->src_addr, 6);
  }

  for (int i = 0; i < cnt; i++) {
    const EarEntry& e = pkt->entries[i];
    if (e.rssi >= 0 || e.rssi < -110) continue;
    int idx = -1;
    for (int k = 0; k < s_nEarRaw; k++) {
      if (memcmp(s_earRaw[k].mac, e.mac, 6) == 0) { idx = k; break; }
    }
    if (idx >= 0) {
      // Peak-biased smoothing on Ear #2 raw packets to reject 3-channel multipath dips
      float m = (float)e.rssi;
      float alpha = (m >= s_earRaw[idx].rssiS) ? 0.45f : (m < s_earRaw[idx].rssiS - 7.0f ? 0.12f : 0.25f);
      s_earRaw[idx].rssiS += (m - s_earRaw[idx].rssiS) * alpha;
      if (e.type) s_earRaw[idx].type = e.type;
      s_earRaw[idx].seenMs = now;
    } else if (s_nEarRaw < MAX_EAR_RAW) {
      EarRaw& nr = s_earRaw[s_nEarRaw++];
      memcpy(nr.mac, e.mac, 6);
      nr.rssiS = (float)e.rssi;
      nr.type = e.type;
      nr.seenMs = now;
    }
  }
  xSemaphoreGive(s_locLock);
}

void locBegin() {
  if (!s_locLock) s_locLock = xSemaphoreCreateMutex();
  loadPrefs();
}

static void initEspNow() {
  if (s_espNowOk) return;
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memset(peer.peer_addr, 0xFF, 6);   // broadcast ACK back to Ear #2 on Channel 6
  peer.channel = 0;                  // current interface channel (6)
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  s_espNowOk = true;
}

static void stopEspNow() {
  if (!s_espNowOk) return;
  esp_now_unregister_recv_cb();
  esp_now_deinit();
  s_espNowOk = false;
  s_earPeerAdded = false;
}

static void locStart() {
  locBegin();
  if (s_on) return;
  crumb("loc start");
  // If Web AP isn't running, bring up WIFI_STA locked to Channel 6 for ESP-NOW
  wifi_mode_t m = WiFi.getMode();
  if (m == WIFI_OFF) {
    wifiRadioOff();
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    s_ownWifi = true;
  } else {
    // Web AP is already on Channel 6; cancel any in-flight scan and ensure channel is pinned to 6
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    s_ownWifi = false;
  }
  esp_wifi_set_ps(WIFI_PS_NONE);          // zero Wi-Fi sleep latency for incoming ESP-NOW packets
  esp_wifi_set_max_tx_power(78);          // max 19.5 dBm radio power on T-Embed
  initEspNow();
  bleRadioOn();
  bleLocatorBoost(true);                  // boost T-Embed BLE scan duty cycle & +9 dBm gain!
  s_on = true;
}

static void locStop() {
  if (!s_on) return;
  crumb("loc stop");
  stopEspNow();
  bleLocatorBoost(false);
  if (s_ownWifi) {
    WiFi.mode(WIFI_OFF);
    bleLowDuty(false);
    bleRadioOff();
    s_ownWifi = false;
  }
  s_on = false;
}

bool locActive() { return s_on; }

bool locWebUse() {
  locStart();
  return s_on;
}

void locWebStop() {
  locStop();
}

void locSetBase(float meters) {
  loadPrefs();
  s_baseM = constrain(meters, 1.0f, 12.0f);
  savePrefs();
}
float locGetBase() { loadPrefs(); return s_baseM; }
int   locGetOffset() { loadPrefs(); return s_offsetDb; }

// 1-tap calibration: uses the user-selected targetId if provided, otherwise picks
// the best centered phone/device (penalizing extreme one-sided desktop PCs glued to one stick!).
bool locCalibrate(String& out, uint32_t targetId) {
  loadPrefs();
  int bestIdx = -1;
  float bestScore = -9999.0f;
  for (int i = 0; i < s_nNodes; i++) {
    const LocNode& n = s_nodes[i];
    if (!n.has1 || !n.has2) continue;
    if (targetId != 0 && n.id == targetId) { bestIdx = i; break; }
    // Prefer phones & balanced signals over a PC sitting right on top of one stick
    float asym = fabsf(n.r1F - (n.r2RawF + 6.0f));
    float typeBonus = (n.rawType == T_PHONE) ? 14.0f : (n.rawType == T_COMPUTER || n.rawType == T_TV ? -8.0f : 0.0f);
    float score = (n.r1F + n.r2RawF) - asym * 0.85f + typeBonus;
    if (score > bestScore) { bestScore = score; bestIdx = i; }
  }
  if (bestIdx < 0) {
    snprintf(s_calMsg, sizeof(s_calMsg), "%s", L("NEED 2 STICKS + PHONE", "НУЖНА СВЯЗЬ С УХОМ #2"));
    s_calToastMs = millis();
    out = "{\"ok\":false,\"err\":\"no shared device heard by both sticks\"}";
    return false;
  }
  const LocNode& b = s_nodes[bestIdx];
  s_offsetDb = constrain((int)lroundf(b.r1F - b.r2RawF), -20, 25);
  savePrefs();
  // Immediately re-anchor r2F on all nodes so calibration takes effect without Kalman lag
  for (int i = 0; i < s_nNodes; i++) {
    if (s_nodes[i].has2) {
      s_nodes[i].r2F = constrain(s_nodes[i].r2RawF + (float)s_offsetDb, -105.0f, -25.0f);
      s_nodes[i].r2 = (int)lroundf(s_nodes[i].r2F);
      s_nodes[i].r2Var = 2.0f;
      s_nodes[i].targetZ = 0.0f;
    }
  }
  snprintf(s_calMsg, sizeof(s_calMsg), L("CAL OK: %+d dB", "КАЛИБРОВКА: %+d дБ"), s_offsetDb);
  s_calToastMs = millis();
  out = "{\"ok\":true,\"ofs\":"; out += s_offsetDb;
  out += ",\"dev\":"; jsonStr(out, b.label[0] ? b.label : (b.name[0] ? b.name : b.type));
  out += '}';
  return true;
}

// Recompute 2D room positions at 5 Hz using sub-dB Kalman RSSI + Hyperbolic Least-Squares solver
static void recomputeNodes(uint32_t now) {
  if (!s_locLock || xSemaphoreTake(s_locLock, pdMS_TO_TICKS(15)) != pdTRUE) return;
  // 1. Prune stale Ear #2 entries (> 12 s)
  int w = 0;
  for (int i = 0; i < s_nEarRaw; i++) {
    if (msSince(now, s_earRaw[i].seenMs) < 12000) s_earRaw[w++] = s_earRaw[i];
  }
  s_nEarRaw = w;

  struct EarMatch { uint32_t id; float r2RawF; uint8_t type; uint8_t mac[6]; bool matched; };
  EarMatch matches[MAX_EAR_RAW];
  int nMatches = 0;
  for (int i = 0; i < s_nEarRaw; i++) {
    uint32_t entId = 0;
    bool m = bleMatchMac(s_earRaw[i].mac, entId);
    if (!m) {
      uint32_t h = 2166136261u;
      for (int b = 0; b < 6; b++) h = (h ^ s_earRaw[i].mac[b]) * 16777619u;
      entId = 0x80000000u | (h & 0x7FFFFFFFu);
    }
    matches[nMatches++] = { entId, s_earRaw[i].rssiS, s_earRaw[i].type,
      { s_earRaw[i].mac[0], s_earRaw[i].mac[1], s_earRaw[i].mac[2], s_earRaw[i].mac[3], s_earRaw[i].mac[4], s_earRaw[i].mac[5] }, m };
  }
  xSemaphoreGive(s_locLock);

  struct SeenCtx {
    uint32_t now;
    EarMatch* matches;
    int nMatches;
  } ctx = { now, matches, nMatches };

  // 2. Update from T-Embed's full BLE scanner table (sub-dB precision)
  bleForEachEnt([](uint32_t id, float rssiF, uint8_t rawType, const char* typeStr, const char* vendor, const char* name, const char* label, void* vctx) {
    SeenCtx* c = (SeenCtx*)vctx;
    int idx = -1;
    for (int i = 0; i < s_nNodes; i++) if (s_nodes[i].id == id) { idx = i; break; }
    if (idx < 0) {
      if (s_nNodes >= MAX_LOC_NODES) return;
      idx = s_nNodes++;
      memset(&s_nodes[idx], 0, sizeof(LocNode));
      s_nodes[idx].id = id;
      s_nodes[idx].x = -s_baseM * 0.25f;
      s_nodes[idx].z = 1.5f;
      s_nodes[idx].err = 1.5f;
    }
    LocNode& n = s_nodes[idx];
    n.has1 = true;
    n.seen1Ms = c->now;
    kalmanRssi(n.r1F, n.r1Var, rssiF);
    n.r1 = (int)lroundf(n.r1F);
    n.rawType = rawType;
    strlcpy(n.type, typeStr ? typeStr : "device", sizeof(n.type));
    strlcpy(n.vendor, vendor ? vendor : "", sizeof(n.vendor));
    strlcpy(n.name, name ? name : "", sizeof(n.name));
    strlcpy(n.label, label ? label : "", sizeof(n.label));
    n.seenMs = c->now;

    for (int k = 0; k < c->nMatches; k++) {
      if (c->matches[k].id == id) {
        n.has2 = true;
        n.seen2Ms = c->now;
        n.r2RawF = c->matches[k].r2RawF;
        float calR2 = constrain(n.r2RawF + (float)s_offsetDb, -105.0f, -25.0f);
        kalmanRssi(n.r2F, n.r2Var, calR2);
        n.r2 = (int)lroundf(n.r2F);
        c->matches[k].id = 0;
        break;
      }
    }
    // Keep 2-stick lock alive across brief 1-packet dropouts (up to 6 s) so nodes never jump between modes
    if (n.has2 && msSince(c->now, n.seen2Ms) > 6500) n.has2 = false;
  }, &ctx);

  // 3. Add remaining Ear-only devices (heard only by M5StickC Plus)
  for (int k = 0; k < nMatches; k++) {
    if (matches[k].id == 0) continue;
    uint32_t id = matches[k].id;
    int idx = -1;
    for (int i = 0; i < s_nNodes; i++) if (s_nodes[i].id == id) { idx = i; break; }
    if (idx < 0) {
      if (s_nNodes >= MAX_LOC_NODES) continue;
      idx = s_nNodes++;
      memset(&s_nodes[idx], 0, sizeof(LocNode));
      s_nodes[idx].id = id;
      s_nodes[idx].x = s_baseM * 0.55f;
      s_nodes[idx].z = 1.5f;
      s_nodes[idx].err = 1.8f;
    }
    LocNode& n = s_nodes[idx];
    if (n.has1 && msSince(now, n.seen1Ms) > 6500) n.has1 = false;
    n.has2 = true;
    n.seen2Ms = now;
    n.r2RawF = matches[k].r2RawF;
    float calR2 = constrain(n.r2RawF + (float)s_offsetDb, -105.0f, -25.0f);
    kalmanRssi(n.r2F, n.r2Var, calR2);
    n.r2 = (int)lroundf(n.r2F);
    if (!n.rawType && matches[k].type) n.rawType = matches[k].type;
    if (!n.type[0] || !strcmp(n.type, "unknown")) strlcpy(n.type, typeName(n.rawType), sizeof(n.type));
    if (!n.name[0]) snprintf(n.name, sizeof(n.name), "EAR %02X:%02X", matches[k].mac[5], matches[k].mac[4]);
    n.seenMs = now;
  }

  // 4. Hyperbolic + Regularized Least-Squares 2D Trilateration
  int nw = 0;
  uint16_t matched = 0;
  const float D = max(1.0f, s_baseM);
  const float halfD = D * 0.5f;

  for (int i = 0; i < s_nNodes; i++) {
    LocNode& n = s_nodes[i];
    if (msSince(now, n.seenMs) > 4000) continue;
    s_nodes[nw++] = n;
  }
  s_nNodes = nw;

  // Sort deterministically by ID BEFORE computing target positions & de-clustering
  // so two nearby nodes NEVER swap indices and oscillate around each other!
  std::sort(s_nodes, s_nodes + s_nNodes, [](const LocNode& a, const LocNode& b) { return a.id < b.id; });

  float rawTx[MAX_LOC_NODES], rawTz[MAX_LOC_NODES], rawErr[MAX_LOC_NODES];
  for (int i = 0; i < s_nNodes; i++) {
    LocNode& n = s_nodes[i];
    float tx, tz, terr;
    float idPhase = ((n.id * 2654435761u) % 1000) / 1000.0f;

    if (n.has1 && n.has2) {
      matched++;
      float d1 = rssiToMeters(n.r1F, n.rawType);
      float d2 = rssiToMeters(n.r2F, n.rawType);
      float sumD = d1 + d2;

      if (sumD <= D * 1.02f) {
        // Near-field between the two sticks: maximum-likelihood projection onto the baseline segment
        // preserves exact proximity ratio (d1 - d2)/(d1 + d2) without pushing close devices away!
        float ratio = (d1 - d2) / max(0.25f, sumD);
        tx = ratio * halfD + (idPhase - 0.5f) * 0.06f;
        tz = constrain(0.18f + 0.24f * sumD + (idPhase - 0.5f) * 0.04f, 0.18f, halfD * 0.85f);
        terr = constrain(0.24f + 0.15f * sumD, 0.24f, 1.2f);
      } else {
        float resid = 0.0f;
        float diffD = d1 - d2;
        if (fabsf(diffD) > D * 0.94f) {
          resid = fabsf(diffD) - D * 0.94f;
          float mid = 0.5f * (d1 + d2);
          float capped = (diffD > 0 ? 1.0f : -1.0f) * D * 0.94f;
          d1 = mid + 0.5f * capped;
          d2 = mid - 0.5f * capped;
        }

        float xTri = (d1 * d1 - d2 * d2) / (2.0f * D);
        float rMean = sqrtf(d1 * d2);
        float sinTh = tanhf((n.r2F - n.r1F) / 11.5f);
        float xHyp = sinTh * max(halfD, rMean * 0.85f);
        tx = xTri * 0.65f + xHyp * 0.35f + (idPhase - 0.5f) * 0.08f;
        tx = constrain(tx, -D * 1.35f, D * 1.35f);

        float dx1 = tx + halfD, dx2 = tx - halfD;
        float z1 = sqrtf(max(0.06f, d1 * d1 - dx1 * dx1));
        float z2 = sqrtf(max(0.06f, d2 * d2 - dx2 * dx2));
        float w1 = 1.0f / (0.3f + d1), w2 = 1.0f / (0.3f + d2);
        tz = (z1 * w1 + z2 * w2) / (w1 + w2) + (idPhase - 0.5f) * 0.06f;
        tz = constrain(tz, 0.22f, D * 2.4f);

        terr = constrain(0.28f + 0.14f * rMean + resid * 0.22f, 0.28f, 2.6f);
      }
    } else if (n.has1) {
      float d1 = rssiToMeters(n.r1F, n.rawType);
      float ang = idPhase * 2.1f - 1.25f;
      tx = -halfD + sinf(ang) * d1 * 0.68f;
      tz = constrain(cosf(ang * 0.6f) * d1, 0.3f, D * 2.1f);
      terr = constrain(0.55f + d1 * 0.3f, 0.55f, 3.2f);
    } else {
      float d2 = rssiToMeters(n.r2F, n.rawType);
      float ang = idPhase * 2.1f - 0.85f;
      tx = halfD + sinf(ang) * d2 * 0.68f;
      tz = constrain(cosf(ang * 0.6f) * d2, 0.3f, D * 2.1f);
      terr = constrain(0.55f + d2 * 0.3f, 0.55f, 3.2f);
    }
    rawTx[i] = tx; rawTz[i] = tz; rawErr[i] = terr;
  }
  s_matchedCnt = matched;

  // 5. Deterministic De-Clustering on target coordinates (before smoothing!)
  const float minSep = constrain(D * 0.10f, 0.20f, 0.40f);
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < s_nNodes; i++) {
      for (int j = i + 1; j < s_nNodes; j++) {
        float dx = rawTx[j] - rawTx[i];
        float dz = rawTz[j] - rawTz[i];
        float dist = hypotf(dx, dz);
        if (dist < minSep) {
          float push = (minSep - dist) * 0.42f;
          // Angle derived deterministically from stable device IDs so it never flips
          float ang = (dist > 0.02f) ? atan2f(dz, dx) : (((s_nodes[i].id ^ s_nodes[j].id) % 360) * 0.017453f);
          float px = cosf(ang) * push, pz = sinf(ang) * push;
          rawTx[i] -= px; rawTz[i] = max(0.22f, rawTz[i] - pz);
          rawTx[j] += px; rawTz[j] = max(0.22f, rawTz[j] + pz);
        }
      }
    }
  }

  // 6. Apply spatial deadband hysteresis + calm exponential glide
  const float deadbandM = constrain(D * 0.065f, 0.15f, 0.30f);
  for (int i = 0; i < s_nNodes; i++) {
    LocNode& n = s_nodes[i];
    if (n.targetZ <= 0.01f) {
      n.targetX = n.x = rawTx[i];
      n.targetZ = n.z = rawTz[i];
      n.err = rawErr[i];
      continue;
    }
    float jump = hypotf(rawTx[i] - n.targetX, rawTz[i] - n.targetZ);
    if (jump > deadbandM) {
      // Smooth breakout beyond deadband
      float kT = min(0.55f, (jump - deadbandM) / (deadbandM * 2.5f));
      n.targetX += (rawTx[i] - n.targetX) * kT;
      n.targetZ += (rawTz[i] - n.targetZ) * kT;
    } else {
      // Micro-creep (2%) inside deadband so stationary devices stay rock-solid
      n.targetX += (rawTx[i] - n.targetX) * 0.02f;
      n.targetZ += (rawTz[i] - n.targetZ) * 0.02f;
    }
    n.x += (n.targetX - n.x) * 0.15f;
    n.z += (n.targetZ - n.z) * 0.15f;
    n.err += (rawErr[i] - n.err) * 0.15f;
  }

  // Sort for display/output: dual-locked devices first, then strongest signal
  std::sort(s_nodes, s_nodes + s_nNodes, [](const LocNode& a, const LocNode& b) {
    float sa = (a.has1 && a.has2 ? 200.0f : 0.0f) + max(a.has1 ? a.r1F : -110.0f, a.has2 ? a.r2F : -110.0f);
    float sb = (b.has1 && b.has2 ? 200.0f : 0.0f) + max(b.has1 ? b.r1F : -110.0f, b.has2 ? b.r2F : -110.0f);
    return sa > sb;
  });
}

void locTick(uint32_t now) {
  if (!s_on) return;
  if (msSince(now, s_calcMs) >= 200) {
    s_calcMs = now;
    recomputeNodes(now);
  }
  // Send heartbeat ACK ("TGNA") every 600 ms (both Unicast with hardware retry + Broadcast)
  if (s_espNowOk && msSince(now, s_ackMs) >= 600) {
    s_ackMs = now;
    AckPacket ack = { {'T', 'G', 'N', 'A'}, 6, 1, s_matchedCnt };
    if (!s_earPeerAdded && (s_earMac[0] | s_earMac[1] | s_earMac[2])) {
      esp_now_peer_info_t p;
      memset(&p, 0, sizeof(p));
      memcpy(p.peer_addr, s_earMac, 6);
      p.channel = 0;
      p.encrypt = false;
      if (esp_now_add_peer(&p) == ESP_OK || esp_now_is_peer_exist(s_earMac)) s_earPeerAdded = true;
    }
    if (s_earPeerAdded) esp_now_send(s_earMac, (const uint8_t*)&ack, sizeof(ack));
    uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    esp_now_send(bcast, (const uint8_t*)&ack, sizeof(ack));
  }
}

// JSON stream for Tablet & Desktop ("loc" event, ~4 Hz)
void locJson(String& o) {
  loadPrefs();
  uint32_t now = millis();
  bool earLive = s_earSeenMs && msSince(now, s_earSeenMs) < 10000;
  int n = min(s_nNodes, 40);
  o.reserve(160 + n * 88);
  o = "{\"on\":"; o += s_on ? 1 : 0;
  o += ",\"base\":"; o += String(s_baseM, 1);
  o += ",\"ofs\":"; o += s_offsetDb;
  o += ",\"ear\":{\"ok\":"; o += earLive ? 1 : 0;
  o += ",\"bat\":"; o += s_earBat;
  o += ",\"usb\":"; o += (s_earFlags & 1) ? 1 : 0;
  o += ",\"rssi\":"; o += s_earLinkRssi;
  o += ",\"pkts\":"; o += s_earPkts;
  o += ",\"ago\":"; o += s_earSeenMs ? msSince(now, s_earSeenMs) / 1000 : 999;
  o += "},\"matched\":"; o += s_matchedCnt;
  o += ",\"d\":[";
  for (int i = 0; i < n; i++) {
    const LocNode& d = s_nodes[i];
    if (i) o += ',';
    // [id, mask(1=stick1,2=stick2,3=both), r1, r2, x, z, err, "type", "vendor", "name", "label"]
    int mask = (d.has1 ? 1 : 0) | (d.has2 ? 2 : 0);
    o += '['; o += d.id; o += ','; o += mask; o += ',';
    o += d.has1 ? d.r1 : -110; o += ','; o += d.has2 ? d.r2 : -110; o += ',';
    o += String(d.x, 2); o += ','; o += String(d.z, 2); o += ','; o += String(d.err, 2); o += ',';
    jsonStr(o, d.type); o += ','; jsonStr(o, d.vendor); o += ',';
    jsonStr(o, d.name); o += ','; jsonStr(o, d.label); o += ']';
  }
  o += "]}";
}

// ---------- Stick Screen (320x170 2D Stereo Room Radar) ----------
static void iconLocator(int x, int y, uint16_t c, uint16_t bg) {
  (void)bg;
  // Two beacons + intersecting range rings
  spr.fillCircle(x - 10, y + 6, 3, c);
  spr.fillCircle(x + 10, y + 6, 3, c);
  spr.drawCircle(x - 10, y + 6, 12, c);
  spr.drawCircle(x + 10, y + 6, 12, c);
  spr.fillCircle(x, y - 3, 3, C_ALERT());
}

static void locSubtitle(char* out, size_t n) {
  uint32_t now = millis();
  bool earLive = s_earSeenMs && msSince(now, s_earSeenMs) < 10000;
  if (s_on && earLive) snprintf(out, n, L("%u locked", "%u пеленг"), (unsigned)s_matchedCnt);
  else if (s_on)       snprintf(out, n, "%s", L("wait ear#2", "жду ухо #2"));
  else                 snprintf(out, n, "%s", L("2x radar", "2 палки"));
}

static void locEnter() { locStart(); }
static void locLeave() { locStop(); }
static void locClick() { String dummy; locCalibrate(dummy); }
static void locRotate(int dir) { locSetBase(s_baseM + dir * 0.5f); }

static void locDraw(uint32_t now) {
  g_fastFrame = true;
  uint16_t bg = C_BG(), acc = C_ACC(), hot = C_HOT(), dim = UI_DIM(), line = UI_LINE(), al = C_ALERT();
  spr.fillSprite(bg);

  bool earLive = s_earSeenMs && msSince(now, s_earSeenMs) < 10000;
  char right[40];
  if (earLive) snprintf(right, sizeof(right), L("EAR#2 %u%% · %.1fM", "УХО#2 %u%% · %.1fМ"), (unsigned)s_earBat, s_baseM);
  else         snprintf(right, sizeof(right), L("WAIT EAR#2 · %.1fM", "ЖДУ УХО#2 · %.1fМ"), s_baseM);
  uiHeader(L("LOCATOR 2X", "ЛОКАТОР 2X"), right, !earLive);

  // Room map area: baseline wall at bottom (yBase = 136), room extends upward (y = 26..136)
  const int mapL = 12, mapR = 308, mapT = 26, mapB = 146;
  const int cx = 160, yBase = 136;
  const float D = max(1.0f, s_baseM);
  const float scale = min(190.0f / D, 96.0f / (D * 1.15f)); // pixels per meter
  const int x1 = cx - (int)roundf(D * 0.5f * scale);
  const int x2 = cx + (int)roundf(D * 0.5f * scale);

  // Subtle range arcs from Stick #1 and Stick #2
  for (int m = 1; m <= 3; m++) {
    int rPx = (int)roundf((D * 0.45f * m) * scale);
    spr.drawCircle(x1, yBase, rPx, line);
    if (earLive) spr.drawCircle(x2, yBase, rPx, line);
  }
  // Baseline link line between Stick #1 and Stick #2
  spr.drawFastHLine(mapL, yBase, mapR - mapL, line);
  if (earLive) {
    spr.drawFastHLine(x1, yBase, x2 - x1, acc);
    // animated pulse dot from Ear #2 to Stick #1
    float p = fmodf(now / 700.0f, 1.0f);
    int px = x2 - (int)roundf((x2 - x1) * p);
    spr.fillCircle(px, yBase, 2, hot);
  }

  // Draw triangulated devices (up to 16 clearest)
  int shown = min(s_nNodes, 16);
  for (int i = shown - 1; i >= 0; i--) {
    const LocNode& d = s_nodes[i];
    int sx = constrain(cx + (int)roundf(d.x * scale), mapL + 6, mapR - 6);
    int sy = constrain(yBase - (int)roundf(d.z * scale), mapT + 6, mapB - 8);
    bool both = d.has1 && d.has2;
    uint16_t col = (d.rawType == T_TRACKER) ? al : (both ? hot : (d.has1 ? acc : dim));
    if (both && i < 4) {
      spr.drawLine(x1, yBase, sx, sy, line);
      spr.drawLine(x2, yBase, sx, sy, line);
      int er = constrain((int)roundf(d.err * scale * 0.45f), 5, 22);
      spr.drawCircle(sx, sy, er, both ? acc : line);
    }
    spr.fillCircle(sx, sy, both ? 3 : 2, col);
    if (i < 5) {
      const char* lbl = d.label[0] ? d.label : (d.name[0] ? d.name : d.type);
      char shortLbl[14];
      fitText(lbl, shortLbl, sizeof(shortLbl), 62, 1);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(col);
      txt(shortLbl, sx, max(mapT + 1, sy - 11), 1);
    }
  }

  // Stick #1 (T-Embed) & Stick #2 (M5StickC Plus) markers
  spr.fillRect(x1 - 4, yBase - 4, 9, 9, acc);
  spr.setTextDatum(TC_DATUM); spr.setTextColor(acc);
  txt("#1", x1, yBase + 6, 1);

  uint16_t c2 = earLive ? al : dim;
  spr.fillRect(x2 - 4, yBase - 4, 9, 9, c2);
  spr.setTextColor(c2);
  txt("#2", x2, yBase + 6, 1);

  // Calibration toast banner if recently pressed
  if (s_calToastMs && msSince(now, s_calToastMs) < 2800) {
    hudPanel(50, 64, 220, 26, 6, acc, true, bg);
    spr.setTextDatum(MC_DATUM); spr.setTextColor(hot);
    txt(s_calMsg, 160, 77, 2);
  }

  char footR[32];
  snprintf(footR, sizeof(footR), L("LOCK:%u/%d · OFS:%+ddB", "ПЕЛЕНГ:%u/%d · %+dдБ"), (unsigned)s_matchedCnt, s_nNodes, s_offsetDb);
  uiFooter(L("WHEEL: BASE · CLICK: CAL", "КОЛЕСО: БАЗА · НАЖАТЬ: КАЛИБР"), footR);
  present();
}

const Section locatorSection = {
  LP("Locator", "Локатор"), 62, 230, 255, iconLocator, locSubtitle,
  locEnter, locLeave,
  0, nullptr, nullptr, nullptr, nullptr,
  locClick, locRotate, nullptr, locTick, locDraw,
};
