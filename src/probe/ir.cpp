// ============================================================
//  ir.cpp — IR receiver via RMT, the IR section and JSON for the tablet.
//  Runs only while the section is open or the tablet is subscribed to "ir".
// ============================================================
#include "probe/ir.h"
#include <Preferences.h>
#include "core/lang.h"
#include "core/jsonutil.h"
#include "core/crumb.h"
#include "core/board.h"
#include "core/leds.h"
#include "core/power.h"
#include "audio/audio.h"

#define IR_RX_PIN 1                   // BOARD_IR_RX (utilities.h LilyGO)
#define IR_TX_PIN 2                   // IR LED: keep it off
#define IR_SYMS   192                 // RMT symbols per burst (4 memory blocks of 48)

static rmt_data_t s_buf[IR_SYMS];
static size_t     s_n = IR_SYMS;
static bool       s_armed = false;
static bool       s_inspect = false;  // true while IR section or tablet IR tab is open
static bool       s_emLearn = false;  // true while Settings -> Emergency is open (binds next IR key)
static uint32_t   s_bgGen = 0;

// Bound emergency IR trigger (persisted in NVS "radar"):
// kind: 0 = none, 1 = decoded protocol (proto+addr+cmd), 2 = 42-bit pulse-distance, 3 = raw hash
static uint8_t    s_emKind = 0;
static char       s_emProto[12] = "";
static uint32_t   s_emAddr = 0, s_emCmd = 0;
static uint64_t   s_emBits = 0;

static void arm() {
  s_n = IR_SYMS;
  s_armed = rmtReadAsync(IR_RX_PIN, s_buf, &s_n);
}

static bool irInitHw() {
  if (g_ir.on) return g_ir.ok;
  crumb("ir start");
  pinMode(IR_TX_PIN, OUTPUT); digitalWrite(IR_TX_PIN, LOW);      // transmitter off
  bool ok = rmtInit(IR_RX_PIN, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_4, 1000000) ||
            rmtInit(IR_RX_PIN, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_2, 1000000);   // 1 tick = 1 µs
  if (ok) {
    rmtSetRxMinThreshold(IR_RX_PIN, 3);                          // shorter than 3 µs = glitch
    rmtSetRxMaxThreshold(IR_RX_PIN, 12000);                      // 12 ms of silence = end of burst
    arm();
  }
  g_ir.ok = ok; g_ir.on = true;
  return ok;
}

void irBegin() {
  Preferences p;
  if (p.begin("radar", true)) {
    s_emKind = (uint8_t)p.getUChar("emk", 0);
    s_emAddr = p.getUInt("ema", 0);
    s_emCmd  = p.getUInt("emc", 0);
    s_emBits = p.getULong64("embits", 0);
    String pr = p.getString("emp", "");
    strlcpy(s_emProto, pr.c_str(), sizeof(s_emProto));
    p.end();
  }
  irInitHw();
  s_bgGen = g_ir.gen;
}

bool irStart() {
  s_inspect = true;
  return irInitHw();
}

void irStop() {
  s_inspect = false;
  s_bgGen = g_ir.gen;                 // ignore bursts captured while inspecting remotes
}

void irEvilSetLearn(bool on) {
  s_emLearn = on;
  s_bgGen = g_ir.gen;
}

void irEvilLabel(char* out, size_t n) {
  switch (s_emKind) {
    case 1: snprintf(out, n, "%.6s %lX:%lX", s_emProto, (unsigned long)s_emAddr, (unsigned long)s_emCmd); break;
    case 2: snprintf(out, n, "42b %04lX", (unsigned long)(s_emBits & 0xFFFF)); break;
    case 3: snprintf(out, n, "RAW %04lX", (unsigned long)(s_emCmd & 0xFFFF)); break;
    default: snprintf(out, n, "%s", L("press IR...", "нажми ИК...")); break;
  }
}

// Jitter-proof pulse-distance decoder for 42-bit NEC-like frames (~9000/4500 header + 42 bits, 560 vs 1690 us gaps)
static bool decodePulse42(const uint16_t* raw, int rawN, uint64_t& out) {
  if (rawN < 85 || rawN > 89) return false;
  if (raw[0] < 7000 || raw[0] > 11500 || raw[1] < 3000 || raw[1] > 6000) return false;
  uint64_t v = 0;
  for (int b = 0; b < 42; b++) {
    uint16_t mk = raw[2 + 2 * b];
    uint16_t sp = raw[2 + 2 * b + 1];
    if (mk < 200 || mk > 1100 || sp < 200 || sp > 2600) return false;
    if (sp > 1100) v |= (1ULL << b);
  }
  out = v;
  return v != 0;
}

static void saveEvilTrigger(const IrCode& c, const uint16_t* raw, int rawN) {
  uint64_t bits = 0;
  if (strcmp(c.proto, "RAW") != 0) {
    s_emKind = 1;
    strlcpy(s_emProto, c.proto, sizeof(s_emProto));
    s_emAddr = c.addr;
    s_emCmd  = c.cmd;
    s_emBits = 0;
  } else if (decodePulse42(raw, rawN, bits)) {
    s_emKind = 2;
    s_emProto[0] = 0;
    s_emAddr = 0;
    s_emCmd  = 0;
    s_emBits = bits;
  } else {
    s_emKind = 3;
    s_emProto[0] = 0;
    s_emAddr = 0;
    s_emCmd  = c.cmd;
    s_emBits = 0;
  }
  Preferences p;
  if (p.begin("radar", false)) {
    p.putUChar("emk", s_emKind);
    p.putString("emp", s_emProto);
    p.putUInt("ema", s_emAddr);
    p.putUInt("emc", s_emCmd);
    p.putULong64("embits", s_emBits);
    p.end();
  }
  audioSiren(1600, 60, 45);
  ledsEvent(2);
}

static bool isEvilTrigger(const IrCode& c, const uint16_t* raw, int rawN) {
  uint64_t bits = 0;
  if (s_emKind == 1) {
    return strcmp(c.proto, s_emProto) == 0 && c.addr == s_emAddr && c.cmd == s_emCmd;
  }
  if (s_emKind == 2) {
    return decodePulse42(raw, rawN, bits) && bits == s_emBits;
  }
  if (s_emKind == 3) {
    return strcmp(c.proto, "RAW") == 0 && c.cmd == s_emCmd;
  }
  return false;
}

void irPollBg(uint32_t now) {
  irTick(now);
  if (g_ir.gen == s_bgGen) return;
  s_bgGen = g_ir.gen;
  if (!g_ir.have || g_ir.last.rep) return;
  if (s_emLearn) {
    saveEvilTrigger(g_ir.last, g_ir.raw, g_ir.rawN);
    return;
  }
  if (isEvilTrigger(g_ir.last, g_ir.raw, g_ir.rawN)) {
    powerEmergency();
  }
}

void irTick(uint32_t now) {
  if (!g_ir.on || !g_ir.ok) return;
  if (!s_armed) { arm(); return; }
  if (!rmtReceiveCompleted(IR_RX_PIN)) return;
  crumb("ir frame");
  // RMT symbol = two segments (level + duration). Receiver: 0 = flash, 1 = gap.
  static uint16_t d[IR_RAW_MAX * 2];
  int n = 0;
  size_t ns = min(s_n, (size_t)IR_SYMS);
  for (size_t i = 0; i < ns && n < (int)(sizeof(d) / sizeof(d[0])); i++) {
    const rmt_data_t& s = s_buf[i];
    uint16_t dur[2] = { (uint16_t)s.duration0, (uint16_t)s.duration1 };
    uint8_t  lv[2]  = { (uint8_t)s.level0, (uint8_t)s.level1 };
    for (int k = 0; k < 2 && n < (int)(sizeof(d) / sizeof(d[0])); k++) {
      if (!dur[k]) continue;
      bool mark = lv[k] == 0;
      if (n == 0 && !mark) continue;                         // start only with a flash
      if ((n % 2 == 0) != mark) {
        if (n) { uint32_t sum = (uint32_t)d[n - 1] + dur[k]; d[n - 1] = sum > 65535 ? 65535 : (uint16_t)sum; }
        continue;                                            // two equal levels in a row: merge without uint16 wrap
      }
      d[n++] = dur[k];
    }
  }
  if (n && n % 2 == 0 && d[n - 1] > 10000) n--;              // trailing silence is not a gap
  if (n >= 3) { irFeed(d, n, now); if (s_inspect) ledsEvent(2); } // ripple LEDs only on IR screen
  arm();
}

// {"p":"NEC","addr":4,"cmd":8,"bits":32,"brand":"LG TV","rep":0,"raw":[9000,4500,...],"t":123456}
void irJson(String& o) {
  const IrModel& m = g_ir;
  o.reserve(420);
  if (!m.have) { o = "{\"p\":\"\",\"ok\":"; o += m.ok ? 1 : 0; o += '}'; return; }
  const IrCode& c = m.last;
  o = "{\"p\":"; jsonStr(o, c.proto);
  o += ",\"addr\":"; o += c.addr; o += ",\"cmd\":"; o += c.cmd; o += ",\"bits\":"; o += c.bits;
  o += ",\"brand\":"; jsonStr(o, c.brand ? c.brand : "");
  o += ",\"rep\":"; o += c.rep ? 1 : 0;
  o += ",\"raw\":[";
  for (int i = 0; i < m.rawN && i < 64; i++) { if (i) o += ','; o += m.raw[i]; }
  o += "],\"t\":"; o += c.t; o += '}';
}

// ---------- section ----------
static void irSubtitle(char* out, size_t n) {
  if (g_ir.have) snprintf(out, n, L("last: %s", "пульт: %s"), g_ir.last.proto);
  else           snprintf(out, n, "38 kHz");
}
static void irEnter() { irStart(); }
static void irLeave() { irStop(); }
static void irClick() { g_ir.histN = 0; g_ir.have = false; g_ir.rawN = 0; }   // press: clear
static void irRotate(int) {}

const Section irSection = {
  LP("IR", "ИК"), 230, 90, 120, iconRemote, irSubtitle,
  irEnter, irLeave,
  0, nullptr, nullptr, nullptr, nullptr,
  irClick, irRotate, nullptr, irTick, drawIr,
};
