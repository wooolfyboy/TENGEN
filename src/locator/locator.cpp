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
  int      r1, r2;         // r2 is after antenna offset calibration
  int      r2Raw;
  float    x, z, err;      // smoothed meters: Stick #1 at (-D/2, 0), Stick #2 at (+D/2, 0), room z > 0
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

// Convert RSSI (dBm) to approximate indoor meters (path-loss exponent n = 2.2, 1m ref = -58 dBm)
static float rssiToMeters(float rssi) {
  float d = powf(10.0f, (-58.0f - rssi) / (10.0f * 2.2f));
  return constrain(d, 0.25f, 14.0f);
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

  for (int i = 0; i < cnt; i++) {
    const EarEntry& e = pkt->entries[i];
    if (e.rssi >= 0 || e.rssi < -110) continue;
    int idx = -1;
    for (int k = 0; k < s_nEarRaw; k++) {
      if (memcmp(s_earRaw[k].mac, e.mac, 6) == 0) { idx = k; break; }
    }
    if (idx >= 0) {
      s_earRaw[idx].rssiS = s_earRaw[idx].rssiS * 0.65f + (float)e.rssi * 0.35f;
      s_earRaw[idx].type = e.type;
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
    // Web AP is already on Channel 6; cancel any in-flight scan so channel stays pinned at 6
    esp_wifi_scan_stop();
    s_ownWifi = false;
  }
  initEspNow();
  bleRadioOn();
  bleLowDuty(true);   // share antenna smoothly between BLE scan and ESP-NOW / AP
  s_on = true;
}

static void locStop() {
  if (!s_on) return;
  crumb("loc stop");
  stopEspNow();
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

// 1-tap calibration: finds the strongest device currently heard by BOTH sticks
// and sets s_offsetDb so both sticks report equal signal for a centered phone.
bool locCalibrate(String& out) {
  loadPrefs();
  int bestIdx = -1, bestScore = -999;
  for (int i = 0; i < s_nNodes; i++) {
    const LocNode& n = s_nodes[i];
    if (!n.has1 || !n.has2) continue;
    int score = n.r1 + n.r2Raw;
    if (score > bestScore) { bestScore = score; bestIdx = i; }
  }
  if (bestIdx < 0) {
    snprintf(s_calMsg, sizeof(s_calMsg), "%s", L("NEED 2 STICKS + PHONE", "НУЖНА СВЯЗЬ С УХОМ #2"));
    s_calToastMs = millis();
    out = "{\"ok\":false,\"err\":\"no shared device heard by both sticks\"}";
    return false;
  }
  const LocNode& b = s_nodes[bestIdx];
  s_offsetDb = constrain(b.r1 - b.r2Raw, -20, 25);
  savePrefs();
  snprintf(s_calMsg, sizeof(s_calMsg), L("CAL OK: %+d dB", "КАЛИБРОВКА: %+d дБ"), s_offsetDb);
  s_calToastMs = millis();
  out = "{\"ok\":true,\"ofs\":"; out += s_offsetDb;
  out += ",\"dev\":"; jsonStr(out, b.label[0] ? b.label : (b.name[0] ? b.name : b.type));
  out += '}';
  return true;
}

// Recompute 2D room positions at 5 Hz
static void recomputeNodes(uint32_t now) {
  if (!s_locLock || xSemaphoreTake(s_locLock, pdMS_TO_TICKS(15)) != pdTRUE) return;
  // 1. Prune stale Ear #2 entries (> 12 s)
  int w = 0;
  for (int i = 0; i < s_nEarRaw; i++) {
    if (msSince(now, s_earRaw[i].seenMs) < 12000) s_earRaw[w++] = s_earRaw[i];
  }
  s_nEarRaw = w;

  // Map Ear #2 MACs to T-Embed entity IDs (or synthetic ear-only IDs 0x80000000 | hash)
  struct EarMatch { uint32_t id; int r2Raw; uint8_t type; uint8_t mac[6]; bool matched; };
  EarMatch matches[MAX_EAR_RAW];
  int nMatches = 0;
  for (int i = 0; i < s_nEarRaw; i++) {
    uint32_t entId = 0;
    bool m = bleMatchMac(s_earRaw[i].mac, entId);
    if (!m) {
      // Ear-only device (heard in far corner by Stick #2, out of T-Embed range)
      uint32_t h = 2166136261u;
      for (int b = 0; b < 6; b++) h = (h ^ s_earRaw[i].mac[b]) * 16777619u;
      entId = 0x80000000u | (h & 0x7FFFFFFFu);
    }
    matches[nMatches++] = { entId, (int)roundf(s_earRaw[i].rssiS), s_earRaw[i].type,
      { s_earRaw[i].mac[0], s_earRaw[i].mac[1], s_earRaw[i].mac[2], s_earRaw[i].mac[3], s_earRaw[i].mac[4], s_earRaw[i].mac[5] }, m };
  }
  xSemaphoreGive(s_locLock);

  // 2. Mark all existing nodes unseen for this pass
  struct SeenCtx {
    uint32_t now;
    EarMatch* matches;
    int nMatches;
  } ctx = { now, matches, nMatches };

  // Update from T-Embed's active BLE entities
  bleForEachEnt([](uint32_t id, int rssi, uint8_t rawType, const char* typeStr, const char* vendor, const char* name, const char* label, void* vctx) {
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
    n.r1 = rssi;
    n.rawType = rawType;
    strlcpy(n.type, typeStr ? typeStr : "device", sizeof(n.type));
    strlcpy(n.vendor, vendor ? vendor : "", sizeof(n.vendor));
    strlcpy(n.name, name ? name : "", sizeof(n.name));
    strlcpy(n.label, label ? label : "", sizeof(n.label));
    n.seenMs = c->now;

    // Check if Ear #2 also hears this entity
    n.has2 = false;
    for (int k = 0; k < c->nMatches; k++) {
      if (c->matches[k].id == id) {
        n.has2 = true;
        n.r2Raw = c->matches[k].r2Raw;
        n.r2 = constrain(n.r2Raw + s_offsetDb, -105, -25);
        c->matches[k].id = 0; // consumed
        break;
      }
    }
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
    n.has1 = false;
    n.has2 = true;
    n.r2Raw = matches[k].r2Raw;
    n.r2 = constrain(n.r2Raw + s_offsetDb, -105, -25);
    n.rawType = matches[k].type;
    strlcpy(n.type, typeName(matches[k].type), sizeof(n.type));
    snprintf(n.name, sizeof(n.name), "EAR %02X:%02X", matches[k].mac[5], matches[k].mac[4]);
    n.seenMs = now;
  }

  // 4. Prune nodes not seen this frame and update 2D trilateration coordinates
  int nw = 0;
  uint16_t matched = 0;
  const float D = max(1.0f, s_baseM);
  const float halfD = D * 0.5f;

  for (int i = 0; i < s_nNodes; i++) {
    LocNode& n = s_nodes[i];
    if (n.seenMs != now) continue;
    float tx, tz, terr;
    if (n.has1 && n.has2) {
      matched++;
      float d1 = rssiToMeters((float)n.r1);
      float d2 = rssiToMeters((float)n.r2);
      // Law of cosines from Stick #1 (-D/2, 0) and Stick #2 (+D/2, 0)
      tx = (d1 * d1 - d2 * d2) / (2.0f * D);
      tx = constrain(tx, -halfD * 1.45f, halfD * 1.45f);
      float dx1 = tx + halfD, dx2 = tx - halfD;
      float z1Sq = d1 * d1 - dx1 * dx1;
      float z2Sq = d2 * d2 - dx2 * dx2;
      float zSq = 0.5f * (max(0.08f, z1Sq) + max(0.08f, z2Sq));
      tz = constrain(sqrtf(zSq), 0.25f, D * 2.2f);
      // Confidence circle radius grows with distance & triangle residual
      float gap = fabsf(d1 + d2 - D);
      terr = constrain(0.35f + 0.18f * (d1 + d2) + (d1 + d2 < D ? (D - d1 - d2) * 0.4f : gap * 0.05f), 0.35f, 3.2f);
    } else if (n.has1) {
      float d1 = rssiToMeters((float)n.r1);
      float ang = ((n.id * 2654435761u) % 1000) / 1000.0f * 2.2f - 1.3f; // biased toward Stick #1 side
      tx = -halfD + sinf(ang) * d1 * 0.65f;
      tz = constrain(cosf(ang * 0.6f) * d1, 0.3f, D * 2.0f);
      terr = constrain(0.6f + d1 * 0.35f, 0.6f, 3.8f);
    } else {
      float d2 = rssiToMeters((float)n.r2);
      float ang = ((n.id * 2654435761u) % 1000) / 1000.0f * 2.2f - 0.9f; // biased toward Stick #2 side
      tx = halfD + sinf(ang) * d2 * 0.65f;
      tz = constrain(cosf(ang * 0.6f) * d2, 0.3f, D * 2.0f);
      terr = constrain(0.6f + d2 * 0.35f, 0.6f, 3.8f);
    }
    n.x += (tx - n.x) * 0.28f;
    n.z += (tz - n.z) * 0.28f;
    n.err += (terr - n.err) * 0.25f;
    s_nodes[nw++] = n;
  }
  s_nNodes = nw;
  s_matchedCnt = matched;

  // Sort: dual-locked devices first, then strongest signal
  std::sort(s_nodes, s_nodes + s_nNodes, [](const LocNode& a, const LocNode& b) {
    int sa = (a.has1 && a.has2 ? 200 : 0) + max(a.has1 ? a.r1 : -110, a.has2 ? a.r2 : -110);
    int sb = (b.has1 && b.has2 ? 200 : 0) + max(b.has1 ? b.r1 : -110, b.has2 ? b.r2 : -110);
    return sa > sb;
  });
}

void locTick(uint32_t now) {
  if (!s_on) return;
  if (msSince(now, s_calcMs) >= 200) {
    s_calcMs = now;
    recomputeNodes(now);
  }
  // Broadcast heartbeat ACK ("TGNA") once a second so M5StickC Plus Ear #2 shows "LINK OK"
  if (s_espNowOk && msSince(now, s_ackMs) >= 900) {
    s_ackMs = now;
    AckPacket ack = { {'T', 'G', 'N', 'A'}, 6, 1, s_matchedCnt };
    uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    esp_now_send(bcast, (const uint8_t*)&ack, sizeof(ack));
  }
}

// JSON stream for Tablet & Desktop ("loc" event, ~4 Hz)
void locJson(String& o) {
  loadPrefs();
  uint32_t now = millis();
  bool earLive = s_earSeenMs && msSince(now, s_earSeenMs) < 4500;
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
  bool earLive = s_earSeenMs && msSince(now, s_earSeenMs) < 4500;
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

  bool earLive = s_earSeenMs && msSince(now, s_earSeenMs) < 4500;
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
