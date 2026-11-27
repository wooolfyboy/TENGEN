// ============================================================
//  devtypes.h — BLE advertisement parsing and device TYPE detection
//
//  Passive only: parses what a device broadcasts (name, Appearance, service
//  UUIDs, manufacturer data). Never connects, never writes.
// ============================================================
#pragma once
#include <Arduino.h>
#include <string.h>
#include <ctype.h>

// ---------- device types ----------
enum DevType : uint8_t {
  T_UNKNOWN = 0,
  T_PHONE,     // phone / tablet
  T_COMPUTER,  // laptop / PC
  T_AUDIO,     // headphones / speakers
  T_WATCH,     // watches / fitness bands
  T_TV,        // TV / set-top boxes
  T_TRACKER,   // trackers (AirTag, SmartTag, Tile...)
  T_BEACON,    // beacons (iBeacon / Eddystone)
  T_INPUT,     // keyboards, mice, gamepads, remotes
  T_SENSOR,    // sensors and smart home
  T_NUM
};

inline const char* typeName(uint8_t t) {
  switch (t) {
    case T_PHONE:    return "phone";
    case T_COMPUTER: return "computer";
    case T_AUDIO:    return "audio";
    case T_WATCH:    return "watch";
    case T_TV:       return "TV";
    case T_TRACKER:  return "tracker";
    case T_BEACON:   return "beacon";
    case T_INPUT:    return "input";
    case T_SENSOR:   return "sensor";
    default:         return "unknown";
  }
}

// type glyph letter (for the monochrome theme)
inline char typeGlyph(uint8_t t) {
  switch (t) {
    case T_PHONE:    return 'P';
    case T_COMPUTER: return 'C';
    case T_AUDIO:    return 'A';
    case T_WATCH:    return 'W';
    case T_TV:       return 'V';
    case T_TRACKER:  return 'T';
    case T_BEACON:   return 'B';
    case T_INPUT:    return 'K';
    case T_SENSOR:   return 'S';
    default:         return '?';
  }
}


// ---------- device intelligence flags (bitmask) ----------
enum DevFlags : uint8_t {
  DF_ACTIVE    = 1 << 0,  // high packet rate or screen-on/unlocked
  DF_MOVING    = 1 << 1,  // RSSI jitter indicates motion / in-hand
  DF_SEPARATED = 1 << 2,  // AirTag / Find My separated from owner
  DF_LID_OPEN  = 1 << 3,  // AirPods / Beats case lid open
  DF_IN_EAR    = 1 << 4,  // AirPods / Beats worn in ear
  DF_HOTSPOT   = 1 << 5,  // Apple Instant Hotspot active
  DF_PAIRING   = 1 << 6,  // Swift Pair / Fast Pair pairing mode
  DF_HAS_TX    = 1 << 7,  // real TX Power read from advertisement
};

// ---------- parsed advertisement ----------
struct AdvInfo {
  bool     hasMfr = false;
  uint16_t cid = 0;           // manufacturer Company ID
  uint8_t  mfr[24] = {0};     // first 24 bytes of manufacturer data (after CID)
  uint8_t  mfrLen = 0;        // full length of manufacturer data (after CID)
  uint16_t uuids[8];          // 16-bit service UUIDs (+ from service data)
  uint8_t  nUuid = 0;
  bool     hasApp = false;
  uint16_t appearance = 0;
  bool     hasTx = false;
  int8_t   txPwr = 127;       // AD 0x0A TX Power Level (dBm)
  uint32_t fastPairId = 0;    // Google Fast Pair 24-bit model ID (0 = none)
  char     name[24] = {0};
  uint8_t  nameLen = 0;
};

// Decoded rich telemetry from a single BLE advertisement
struct BleIntel {
  uint8_t  flags = 0;         // DF_* bits
  int8_t   txPwr = 127;       // 127 = unknown
  uint8_t  bat = 255;         // 0..100 overall battery, 255 = unknown
  uint8_t  batL = 255;        // AirPods left (0..100, 255 = unknown)
  uint8_t  batR = 255;        // AirPods right
  uint8_t  batC = 255;        // AirPods case
  char     model[22] = {0};   // e.g. "AirPods Pro 2", "WH-1000XM5"
  char     state[24] = {0};   // e.g. "Screen on", "AirDrop", "Swift Pair"
};

inline void addUuid(AdvInfo& a, uint16_t u) {
  for (uint8_t i = 0; i < a.nUuid; i++) if (a.uuids[i] == u) return;
  if (a.nUuid < 8) a.uuids[a.nUuid++] = u;
}

// Parse a raw packet: sequence of [len][type][data...]
inline void parseAdv(const uint8_t* p, size_t n, AdvInfo& a) {
  size_t i = 0;
  while (i + 1 < n) {
    uint8_t L = p[i];
    if (L == 0 || i + 1 + L > n) break;
    uint8_t t = p[i + 1];
    const uint8_t* d = p + i + 2;
    uint8_t dl = L - 1;
    switch (t) {
      case 0x02: case 0x03:                          // 16-bit UUID list
        for (uint8_t k = 0; k + 1 < dl; k += 2) addUuid(a, d[k] | (d[k + 1] << 8));
        break;
      case 0x0A:                                     // TX Power Level (dBm)
        if (dl >= 1) {
          int8_t tp = (int8_t)d[0];
          if (tp >= -100 && tp <= 20) { a.hasTx = true; a.txPwr = tp; }
        }
        break;
      case 0x16:                                     // service data (16-bit UUID)
        if (dl >= 2) {
          uint16_t u = d[0] | (d[1] << 8);
          addUuid(a, u);
          // Google Fast Pair: 0xFE2C with 3-byte model ID
          if (u == 0xFE2C && dl == 5) {
            a.fastPairId = ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 8) | d[4];
          }
        }
        break;
      case 0x08: case 0x09:                          // name (short / complete)
        if (t == 0x09 || a.nameLen == 0) {
          uint8_t m = dl < sizeof(a.name) - 1 ? dl : sizeof(a.name) - 1;
          memcpy(a.name, d, m); a.name[m] = 0; a.nameLen = m;
        }
        break;
      case 0x19:                                     // appearance
        if (dl >= 2) { a.appearance = d[0] | (d[1] << 8); a.hasApp = true; }
        break;
      case 0xFF:                                     // manufacturer data
        if (dl >= 2 && !a.hasMfr) {
          a.hasMfr = true;
          a.cid = d[0] | (d[1] << 8);
          a.mfrLen = dl - 2;
          uint8_t m = a.mfrLen < sizeof(a.mfr) ? a.mfrLen : sizeof(a.mfr);
          memcpy(a.mfr, d + 2, m);
        }
        break;
    }
    i += 1 + L;
  }
}

// ---------- Apple AirPods / Beats model lookup ----------
inline const char* appleAudioModel(uint16_t id) {
  switch (id) {
    case 0x0220: return "AirPods 1";
    case 0x0F20: return "AirPods 2";
    case 0x1320: return "AirPods 3";
    case 0x1920: return "AirPods 4";
    case 0x1B20: return "AirPods 4 ANC";
    case 0x0E20: return "AirPods Pro";
    case 0x1420: return "AirPods Pro 2";
    case 0x2420: return "AirPods Pro 2C";
    case 0x0A20: return "AirPods Max";
    case 0x0520: return "BeatsX";
    case 0x0620: return "Beats Solo3";
    case 0x0920: return "Beats Studio3";
    case 0x0B20: return "Powerbeats Pro";
    case 0x1020: return "Beats Flex";
    case 0x1120: return "Studio Buds";
    case 0x1220: return "Beats Fit Pro";
    case 0x1620: return "Studio Buds+";
    case 0x1720: return "Beats Studio Pro";
    default:     return nullptr;
  }
}

// ---------- Popular Google Fast Pair 24-bit model lookup ----------
inline const char* fastPairModel(uint32_t id) {
  switch (id) {
    case 0xCD8256: return "Sony WF-1000XM4";
    case 0x821F66: return "Sony WH-1000XM5";
    case 0xD446A7: return "Sony WH-1000XM4";
    case 0x01E5CE: return "Sony WF-1000XM5";
    case 0x0582FD: return "Pixel Buds A";
    case 0x9ADB11: return "Pixel Buds Pro";
    case 0x06AE20: return "Galaxy Buds2 Pro";
    case 0x00000A: return "Galaxy Buds2";
    case 0xF52494: return "JBL Tune Buds";
    case 0x718FA4: return "JBL Flip 6";
    case 0x02D815: return "Nothing Ear (2)";
    case 0x5A36A5: return "Bose QC Ultra";
    case 0x000006: return "Google Fast Pair";
    default:       return nullptr;
  }
}

// ---------- Deep passive decoder (Apple Continuity, AirPods, Find My, Fast/Swift Pair) ----------
inline void decodeIntel(const AdvInfo& a, BleIntel& out) {
  if (a.hasTx) {
    out.txPwr = a.txPwr;
    out.flags |= DF_HAS_TX;
  }
  if (a.fastPairId) {
    out.flags |= DF_PAIRING;
    const char* fm = fastPairModel(a.fastPairId);
    if (fm) strlcpy(out.model, fm, sizeof(out.model));
    strlcpy(out.state, "Fast Pair", sizeof(out.state));
  }
  if (!a.hasMfr || a.mfrLen < 2) return;

  // Microsoft Swift Pair (0x0006, beacon type 0x03)
  if (a.cid == 0x0006 && a.mfr[0] == 0x03) {
    out.flags |= DF_PAIRING;
    strlcpy(out.state, "Swift Pair", sizeof(out.state));
    return;
  }

  // Apple Continuity (0x004C)
  if (a.cid != 0x004C) return;
  uint8_t msgType = a.mfr[0];
  uint8_t msgLen  = a.mfr[1];

  // 0x07: AirPods / Beats Proximity Pairing
  if (msgType == 0x07 && a.mfrLen >= 10) {
    uint16_t mid = ((uint16_t)a.mfr[3] << 8) | a.mfr[4];
    const char* am = appleAudioModel(mid);
    if (am) strlcpy(out.model, am, sizeof(out.model));
    uint8_t st = a.mfr[5];
    bool flipped = (st & 0x20) == 0;
    uint8_t b0 = a.mfr[6] & 0x0F;         // primary pod (0..10 -> 0..100%, 15 = N/A)
    uint8_t b1 = (a.mfr[6] >> 4) & 0x0F;  // secondary pod
    uint8_t bc = a.mfr[7] & 0x0F;         // case
    uint8_t lRaw = flipped ? b1 : b0;
    uint8_t rRaw = flipped ? b0 : b1;
    if (lRaw <= 10) out.batL = lRaw * 10;
    if (rRaw <= 10) out.batR = rRaw * 10;
    if (bc   <= 10) out.batC = bc   * 10;
    if (out.batL <= 100 && out.batR <= 100) out.bat = (out.batL + out.batR) / 2;
    else if (out.batL <= 100) out.bat = out.batL;
    else if (out.batR <= 100) out.bat = out.batR;
    else if (out.batC <= 100) out.bat = out.batC;
    if (st & 0x0A) out.flags |= DF_IN_EAR;
    if (a.mfrLen >= 9 && (a.mfr[8] & 0x08) == 0) out.flags |= DF_LID_OPEN;
    if (out.flags & DF_IN_EAR)        strlcpy(out.state, "In ear", sizeof(out.state));
    else if (out.flags & DF_LID_OPEN) strlcpy(out.state, "Case open", sizeof(out.state));
    else                              strlcpy(out.state, "In case", sizeof(out.state));
    return;
  }

  // 0x10: Nearby Info (iPhone / iPad / Mac screen & activity status)
  if (msgType == 0x10 && a.mfrLen >= 4) {
    uint8_t act = a.mfr[2] & 0x0F;
    switch (act) {
      case 0x00: strlcpy(out.state, "Screen off", sizeof(out.state)); break;
      case 0x01: case 0x03: strlcpy(out.state, "Lock screen", sizeof(out.state)); out.flags |= DF_ACTIVE; break;
      case 0x05: case 0x07: case 0x0B: strlcpy(out.state, "Screen on", sizeof(out.state)); out.flags |= DF_ACTIVE; break;
      case 0x09: strlcpy(out.state, "Media / Video", sizeof(out.state)); out.flags |= DF_ACTIVE; break;
      case 0x0A: strlcpy(out.state, "Active / Call", sizeof(out.state)); out.flags |= DF_ACTIVE; break;
      default:   break;
    }
    return;
  }

  // 0x0E: Instant Hotspot (battery + cellular)
  if (msgType == 0x0E && a.mfrLen >= 8) {
    out.flags |= DF_HOTSPOT | DF_ACTIVE;
    if (a.mfr[4] <= 100) out.bat = a.mfr[4];
    strlcpy(out.state, "Hotspot", sizeof(out.state));
    return;
  }

  // 0x05: AirDrop
  if (msgType == 0x05) {
    out.flags |= DF_ACTIVE;
    strlcpy(out.state, "AirDrop", sizeof(out.state));
    return;
  }

  // 0x12: Find My / AirTag (nearby 2-byte vs separated 25-byte offline finding!)
  if (msgType == 0x12 && a.mfrLen >= 3) {
    uint8_t status = a.mfr[2];
    uint8_t bCode = (status >> 6) & 0x03;
    static const uint8_t BAT_MAP[4] = { 100, 65, 25, 10 };
    out.bat = BAT_MAP[bCode];
    if (msgLen >= 0x19) {
      out.flags |= DF_SEPARATED;
      strlcpy(out.state, "Separated!", sizeof(out.state));
    } else {
      strlcpy(out.state, "Near owner", sizeof(out.state));
    }
    return;
  }

  // 0x02: iBeacon (measured 1m RSSI at byte 22 -> convert to equivalent TX power)
  if (msgType == 0x02 && a.mfrLen >= 23) {
    int8_t meas1m = (int8_t)a.mfr[22];
    if (meas1m <= -35 && meas1m >= -95) {
      out.txPwr = (int8_t)constrain((int)meas1m + 58, -40, 20);
      out.flags |= DF_HAS_TX;
    }
  }
}


// ---------- by Appearance (category = value >> 6) ----------
inline uint8_t typeFromAppearance(uint16_t app) {
  switch (app >> 6) {
    case 1:  return T_PHONE;
    case 2:  return T_COMPUTER;
    case 3:  return T_WATCH;
    case 5:  return T_TV;          // display
    case 6:  return T_INPUT;       // remote control
    case 8:  case 9: return T_TRACKER;   // tag / keyring
    case 10: return T_AUDIO;       // media player
    case 15: return T_INPUT;       // HID
    case 33: case 34: case 37: return T_AUDIO;  // audio sink / source / wearable audio
    case 81: return T_WATCH;       // outdoor sports activity
    case 12: case 13: case 14: case 16: case 17: case 18:
    case 21: case 22: case 23: case 24: case 25: case 26:
    case 49: case 50: case 51:
      return T_SENSOR;             // thermometers, heart rate, sensors, light, smart home
    default: return T_UNKNOWN;
  }
}

// ---------- by name (keywords) ----------
inline bool nameHas(const char* low, const char* const* words) {
  for (int i = 0; words[i]; i++) if (strstr(low, words[i])) return true;
  return false;
}

inline uint8_t typeFromName(const char* name) {
  if (!name[0]) return T_UNKNOWN;
  char low[24];
  size_t i = 0;
  for (; i < sizeof(low) - 1 && name[i]; i++) low[i] = (char)tolower((unsigned char)name[i]);
  low[i] = 0;

  static const char* const trk[] = { "tile", "smarttag", "chipolo", "airtag", nullptr };
  static const char* const aud[] = { "airpods", "buds", "beats", "jbl", "bose", "headphone",
    "headset", "earbud", "speaker", "soundbar", "soundcore", "marshall", "wh-", "wf-",
    "tws", "pods", "audio", "freebuds", "sound", nullptr };
  static const char* const wch[] = { "watch", "band", "amazfit", "fitbit", "garmin",
    "forerunner", "fenix", "gtr", "gts", nullptr };
  static const char* const tv[]  = { "tv", "bravia", "chromecast", "roku", "tizen",
    "webos", "projector", nullptr };
  static const char* const inp[] = { "keyboard", "mouse", "mx master", "mx keys",
    "controller", "gamepad", "xbox", "dualsense", "dualshock", "joy-con", "remote", "pencil", nullptr };
  static const char* const sen[] = { "lywsd", "atc_", "thermo", "sensor", "hygro", "scale",
    "bthome", "flower", "plant", "bulb", "lamp", "light", "mibfs", "switchbot", nullptr };
  static const char* const pc[]  = { "macbook", "laptop", "desktop", "imac", "thinkpad",
    "surface", "ipad", "tablet", nullptr };
  static const char* const ph[]  = { "iphone", "galaxy", "pixel", "redmi", "xiaomi",
    "phone", "oneplus", "honor", "huawei", "poco", "realme", "oppo", "vivo", nullptr };

  // order matters: "galaxy buds" are earbuds, not a phone
  if (nameHas(low, trk)) return T_TRACKER;
  if (nameHas(low, aud)) return T_AUDIO;
  if (nameHas(low, wch)) return T_WATCH;
  if (nameHas(low, tv))  return T_TV;
  if (nameHas(low, inp)) return T_INPUT;
  if (nameHas(low, sen)) return T_SENSOR;
  if (nameHas(low, pc))  return T_COMPUTER;
  if (nameHas(low, ph))  return T_PHONE;
  return T_UNKNOWN;
}

// ---------- by service UUIDs ----------
inline bool isTrackerUuid(uint16_t u) {
  return u == 0xFEED || u == 0xFEEC ||   // Tile
         u == 0xFD5A ||                  // Samsung SmartTag
         u == 0xFE33;                    // Chipolo
}

inline uint8_t typeFromUuids(const AdvInfo& a) {
  for (uint8_t i = 0; i < a.nUuid; i++) {
    switch (a.uuids[i]) {
      case 0xFEAA: return T_BEACON;                    // Eddystone
      case 0xFE2C: return T_AUDIO;                     // Google Fast Pair (usually earbuds)
      case 0x184E: case 0x184F: case 0x1853: return T_AUDIO;  // LE Audio
      case 0xFE07: return T_AUDIO;                     // Sonos
      case 0x1812: return T_INPUT;                     // HID
      case 0x180D: return T_WATCH;                     // heart rate (watch / chest strap)
      case 0xFD6F: return T_PHONE;                     // contact tracing (phones)
      case 0x181A: case 0xFCD2: case 0xFE95:           // environment sensors, BTHome, Xiaomi
      case 0x1809: case 0x1810: case 0x1808: case 0x181D:
      case 0x1816: case 0x1814:
        return T_SENSOR;
    }
  }
  return T_UNKNOWN;
}

// ---------- by manufacturer data ----------
inline uint8_t typeFromMfr(const AdvInfo& a) {
  if (!a.hasMfr) return T_UNKNOWN;
  if (a.cid == 0x004C && a.mfrLen >= 1) {          // Apple: first byte = message type
    switch (a.mfr[0]) {
      case 0x02: return T_BEACON;                  // iBeacon
      case 0x07: return T_AUDIO;                   // AirPods / Beats (proximity pairing)
      case 0x09: return T_TV;                      // AirPlay target (Apple TV etc.)
      case 0x0B: return T_WATCH;                   // Apple Watch
      case 0x06: return T_SENSOR;                  // HomeKit
      case 0x12: {                                 // Find My
        if (a.mfrLen >= 3) {
          switch ((a.mfr[2] >> 4) & 0x03) {        // status byte: who is broadcasting
            case 1: case 2: return T_TRACKER;      // AirTag / third-party Find My tag
            case 3: return T_AUDIO;                // AirPods
            default: return T_PHONE;               // iPhone / iPad / Mac
          }
        }
        return T_TRACKER;
      }
      case 0x05: case 0x0A: case 0x0C: case 0x0D:
      case 0x0E: case 0x0F: case 0x10:
        return T_PHONE;                            // AirDrop / Handoff / Nearby: iPhone etc.
    }
    return T_UNKNOWN;
  }
  if (a.cid == 0x0006 && a.mfrLen >= 1 && a.mfr[0] == 0x01) return T_COMPUTER;  // Windows PC
  switch (a.cid) {
    case 0x0087: return T_WATCH;    // Garmin
    case 0x0157: return T_WATCH;    // Huami (Amazfit / Mi Band)
    case 0x009E: return T_AUDIO;    // Bose
    case 0x0057: return T_AUDIO;    // Harman (JBL)
  }
  return T_UNKNOWN;
}

// ---------- final classification ----------
inline uint8_t classify(const AdvInfo& a) {
  for (uint8_t i = 0; i < a.nUuid; i++) if (isTrackerUuid(a.uuids[i])) return T_TRACKER;
  uint8_t t;
  if (a.hasApp && (t = typeFromAppearance(a.appearance)) != T_UNKNOWN) return t;
  if ((t = typeFromName(a.name)) != T_UNKNOWN) return t;
  if ((t = typeFromUuids(a)) != T_UNKNOWN) return t;
  return typeFromMfr(a);
}

// ---------- advertisement fingerprint for merging address rotations ----------
// Only what stays constant for a device between packets:
// manufacturer, message type, data length, services, name.
// 0 = empty fingerprint (nothing to merge on).
inline uint32_t fnv(uint32_t h, uint32_t v) { h ^= v; return h * 16777619u; }

inline uint32_t advSignature(const AdvInfo& a) {
  if (!a.hasMfr && a.nUuid == 0 && a.nameLen == 0 && !a.hasApp) return 0;
  uint32_t h = 2166136261u;
  if (a.hasMfr) {
    h = fnv(h, a.cid);
    h = fnv(h, a.mfrLen);
    if (a.mfrLen >= 1) h = fnv(h, a.mfr[0]);
  }
  // UUIDs in sorted order
  uint16_t u[8]; uint8_t n = a.nUuid;
  memcpy(u, a.uuids, n * sizeof(uint16_t));
  for (uint8_t i = 1; i < n; i++)
    for (uint8_t j = i; j > 0 && u[j - 1] > u[j]; j--) { uint16_t x = u[j]; u[j] = u[j - 1]; u[j - 1] = x; }
  for (uint8_t i = 0; i < n; i++) h = fnv(h, u[i]);
  if (a.hasApp) h = fnv(h, a.appearance);
  for (uint8_t i = 0; i < a.nameLen; i++) h = fnv(h, (uint8_t)a.name[i]);
  return h ? h : 1;
}
