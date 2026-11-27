// ============================================================
//  sensors.h — reading BLE thermometer values from their advertisements
//
//  Many sensors broadcast temperature/humidity unencrypted to everyone
//  around. This file only parses those packets.
//
//  Supported:
//   - Xiaomi LYWSD03MMC etc. with PVVX / ATC1441 firmware (UUID 0x181A)
//   - BTHome v2 (UUID 0xFCD2): Shelly, PVVX in BTHome mode, etc.
//   - Xiaomi MiBeacon, unencrypted (UUID 0xFE95): older models
//   - Qingping / ClearGrass (UUID 0xFDCD)
//   - SwitchBot Meter (UUID 0xFD3D)
//   - Govee H5072/H5075/H5101/... (manufacturer 0xEC88)
//   - RuuviTag, format 5 (manufacturer 0x0499)
// ============================================================
#pragma once
#include <Arduino.h>
#include <string.h>

struct SensorReading {
  bool  hasT = false, hasH = false, hasB = false, hasP = false, hasCO2 = false;
  bool  hasDoor = false, doorOpen = false;
  bool  hasMotion = false, motion = false;
  bool  hasLeak = false, leak = false;
  bool  hasLux = false;
  float temp = 0;      // °C
  float hum  = 0;      // %
  int   batt = 0;      // %
  float press = 0;     // hPa
  int   co2 = 0;       // ppm
  float lux = 0;       // lx
  const char* model = "";
  bool any() const { return hasT || hasH || hasP || hasCO2 || hasDoor || hasMotion || hasLeak || hasLux; }
};

static inline int16_t s16le(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }
static inline uint16_t u16le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline int16_t s16be(const uint8_t* p) { return (int16_t)((p[0] << 8) | p[1]); }
static inline uint16_t u16be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

// --- 0x181A: PVVX (15 bytes) or ATC1441 (13 bytes) ---
static inline bool parse181A(const uint8_t* d, uint8_t n, SensorReading& r) {
  if (n >= 15) {                     // PVVX: MAC(6,LE) t(s16,0.01) h(u16,0.01) mV(u16) %(u8) cnt flg
    r.temp = s16le(d + 6) / 100.0f; r.hasT = true;
    r.hum  = u16le(d + 8) / 100.0f; r.hasH = true;
    r.batt = d[12];                 r.hasB = true;
    r.model = "PVVX";
    return true;
  }
  if (n >= 13) {                     // ATC1441: MAC(6,BE) t(s16 BE,0.1) h(u8) %(u8) mV(u16 BE) cnt
    r.temp = s16be(d + 6) / 10.0f; r.hasT = true;
    r.hum  = d[8];                 r.hasH = true;
    r.batt = d[9];                 r.hasB = true;
    r.model = "ATC";
    return true;
  }
  return false;
}

// --- 0xFCD2: BTHome v2 ---
// data length per object ID (0 = unknown -> stop parsing)
static inline uint8_t bthomeLen(uint8_t id) {
  if (id >= 0x15 && id <= 0x2F) return 1;              // binary sensors, humidity/moisture uint8
  switch (id) {
    case 0x00: case 0x01: case 0x09: case 0x0F: case 0x10: case 0x11:
    case 0x3A: case 0x46: return 1;
    case 0x02: case 0x03: case 0x06: case 0x07: case 0x08: case 0x0C:
    case 0x0D: case 0x0E: case 0x12: case 0x13: case 0x14: case 0x3C:
    case 0x3D: case 0x3F: case 0x40: case 0x41: case 0x43: case 0x44:
    case 0x45: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x51:
    case 0x52: return 2;
    case 0x04: case 0x05: case 0x0A: case 0x0B: case 0x42: case 0x4B: return 3;
    case 0x3E: case 0x4C: case 0x4D: case 0x4E: case 0x4F: case 0x50: return 4;
  }
  return 0;
}

static inline bool parseBTHome(const uint8_t* d, uint8_t n, SensorReading& r) {
  if (n < 1) return false;
  uint8_t info = d[0];
  if (info & 0x01) return false;               // encrypted: skip
  if (((info >> 5) & 0x07) != 2) return false; // v2 only
  uint8_t i = 1;
  while (i < n) {
    uint8_t id = d[i++];
    uint8_t L = bthomeLen(id);
    if (L == 0 || i + L > n) break;
    const uint8_t* v = d + i;
    switch (id) {
      case 0x01: r.batt = v[0]; r.hasB = true; break;
      case 0x02: r.temp = s16le(v) / 100.0f; r.hasT = true; break;
      case 0x45: r.temp = s16le(v) / 10.0f;  r.hasT = true; break;
      case 0x03: r.hum  = u16le(v) / 100.0f; r.hasH = true; break;
      case 0x2E: r.hum  = v[0];              r.hasH = true; break;
      case 0x04: r.press = (v[0] | (v[1] << 8) | ((uint32_t)v[2] << 16)) / 100.0f; r.hasP = true; break;
      case 0x05: r.lux = (v[0] | (v[1] << 8) | ((uint32_t)v[2] << 16)) / 100.0f; r.hasLux = true; break;
      case 0x12: r.co2 = u16le(v); r.hasCO2 = true; break;
      case 0x1A: case 0x2D: r.doorOpen = v[0] != 0; r.hasDoor = true; break;   // door / window
      case 0x20: r.leak = v[0] != 0; r.hasLeak = true; break;                  // moisture / leak
      case 0x21: r.motion = v[0] != 0; r.hasMotion = true; break;              // motion
    }
    i += L;
  }
  r.model = "BTHome";
  return r.any();
}

// --- 0xFE95: Xiaomi MiBeacon (unencrypted only) ---
static inline bool parseMiBeacon(const uint8_t* d, uint8_t n, SensorReading& r) {
  if (n < 5) return false;
  uint16_t fc = u16le(d);
  if (fc & 0x0008) return false;               // encrypted
  if (!(fc & 0x0040)) return false;            // no data object
  uint8_t i = 5;                               // fc(2) + product id(2) + counter(1)
  if (fc & 0x0010) i += 6;                     // MAC
  if (fc & 0x0020) {                           // capability (+1 byte if IO present)
    if (i >= n) return false;
    uint8_t cap = d[i++];
    if (cap & 0x20) i += 1;
  }
  while (i + 3 <= n) {
    uint16_t id = u16le(d + i); uint8_t L = d[i + 2]; i += 3;
    if (i + L > n) break;
    const uint8_t* v = d + i;
    switch (id) {
      case 0x1004: if (L >= 2) { r.temp = s16le(v) / 10.0f; r.hasT = true; } break;
      case 0x1006: if (L >= 2) { r.hum  = u16le(v) / 10.0f; r.hasH = true; } break;
      case 0x100A: if (L >= 1) { r.batt = v[0]; r.hasB = true; } break;
      case 0x100D: if (L >= 4) { r.temp = s16le(v) / 10.0f; r.hum = u16le(v + 2) / 10.0f;
                                 r.hasT = r.hasH = true; } break;
      case 0x1007: if (L >= 3) { r.lux = (float)(v[0] | (v[1] << 8) | ((uint32_t)v[2] << 16)); r.hasLux = true; } break;
      case 0x1019: if (L >= 1) { r.doorOpen = (v[0] == 0); r.hasDoor = true; } break;
      case 0x000F: if (L >= 1) { r.motion = (v[0] != 0); r.hasMotion = true; } break;
    }
    i += L;
  }
  r.model = "Xiaomi";
  return r.any();
}

// --- 0xFDCD: Qingping ---
static inline bool parseQingping(const uint8_t* d, uint8_t n, SensorReading& r) {
  if (n < 8) return false;
  uint8_t i = 8;                               // flags(1) + type(1) + MAC(6)
  while (i + 2 <= n) {
    uint8_t id = d[i], L = d[i + 1]; i += 2;
    if (i + L > n) break;
    const uint8_t* v = d + i;
    switch (id) {
      case 0x01: if (L >= 4) { r.temp = s16le(v) / 10.0f; r.hum = u16le(v + 2) / 10.0f;
                               r.hasT = r.hasH = true; } break;
      case 0x02: if (L >= 1) { r.batt = v[0]; r.hasB = true; } break;
      case 0x07: if (L >= 2) { r.press = u16le(v) / 10.0f; r.hasP = true; } break;
      case 0x12: if (L >= 2) { r.co2 = u16le(v); r.hasCO2 = true; } break;
    }
    i += L;
  }
  r.model = "Qingping";
  return r.any();
}

// --- 0xFD3D: SwitchBot Meter ---
static inline bool parseSwitchBot(const uint8_t* d, uint8_t n, SensorReading& r) {
  if (n < 6) return false;
  uint8_t model = d[0] & 0x7F;
  if (model != 'T' && model != 'i' && model != 'w') return false;   // Meter / Meter+ / Outdoor
  r.batt = d[2] & 0x7F; r.hasB = true;
  float t = (d[4] & 0x7F) + (d[3] & 0x0F) / 10.0f;
  r.temp = (d[4] & 0x80) ? t : -t; r.hasT = true;
  r.hum = d[5] & 0x7F; r.hasH = true;
  r.model = "SwitchBot";
  return true;
}

// --- manufacturer 0xEC88: Govee ---
static inline bool parseGovee(const uint8_t* m, uint8_t n, SensorReading& r) {
  if (n < 5) return false;                     // m = data AFTER the company id
  uint32_t raw = ((uint32_t)m[1] << 16) | ((uint32_t)m[2] << 8) | m[3];
  bool neg = raw & 0x800000;
  raw &= 0x7FFFFF;
  r.temp = (raw / 1000) / 10.0f; if (neg) r.temp = -r.temp;
  r.hum  = (raw % 1000) / 10.0f;
  r.batt = m[4] & 0x7F;
  r.hasT = r.hasH = r.hasB = true;
  r.model = "Govee";
  return r.temp > -40 && r.temp < 80;
}

// --- manufacturer 0x0499: RuuviTag format 5 ---
static inline bool parseRuuvi(const uint8_t* m, uint8_t n, SensorReading& r) {
  if (n < 15 || m[0] != 0x05) return false;
  int16_t t = s16be(m + 1);
  if (t != (int16_t)0x8000) { r.temp = t * 0.005f; r.hasT = true; }
  uint16_t h = u16be(m + 3);
  if (h != 0xFFFF) { r.hum = h * 0.0025f; r.hasH = true; }
  uint16_t p = u16be(m + 5);
  if (p != 0xFFFF) { r.press = (p + 50000) / 100.0f; r.hasP = true; }
  r.model = "Ruuvi";
  return r.any();
}

// Entry point: tries all formats on a raw [len][type][data]... packet
static inline bool parseSensor(const uint8_t* p, size_t n, SensorReading& r) {
  size_t i = 0;
  bool ok = false;
  while (i + 1 < n) {
    uint8_t L = p[i];
    if (L == 0 || i + 1 + L > n) break;
    uint8_t t = p[i + 1];
    const uint8_t* d = p + i + 2;
    uint8_t dl = L - 1;
    if (t == 0x16 && dl >= 2) {                // service data: UUID(2) + data
      uint16_t u = u16le(d);
      const uint8_t* v = d + 2; uint8_t vl = dl - 2;
      switch (u) {
        case 0x181A: ok |= parse181A(v, vl, r); break;
        case 0xFCD2: ok |= parseBTHome(v, vl, r); break;
        case 0xFE95: ok |= parseMiBeacon(v, vl, r); break;
        case 0xFDCD: ok |= parseQingping(v, vl, r); break;
        case 0xFD3D: ok |= parseSwitchBot(v, vl, r); break;
      }
    } else if (t == 0xFF && dl >= 2) {         // manufacturer data: CID(2) + data
      uint16_t cid = u16le(d);
      if (cid == 0xEC88) ok |= parseGovee(d + 2, dl - 2, r);
      else if (cid == 0x0499) ok |= parseRuuvi(d + 2, dl - 2, r);
    }
    i += 1 + L;
  }
  return ok && r.any();
}
