// ============================================================
//  ir.cpp — IR receiver via RMT, the IR section and JSON for the tablet.
//  Runs only while the section is open or the tablet is subscribed to "ir".
// ============================================================
#include "probe/ir.h"
#include "core/lang.h"
#include "core/jsonutil.h"
#include "core/crumb.h"
#include "core/board.h"

#define IR_RX_PIN 1                   // BOARD_IR_RX (utilities.h LilyGO)
#define IR_TX_PIN 2                   // IR LED: keep it off
#define IR_SYMS   192                 // RMT symbols per burst (4 memory blocks of 48)

static rmt_data_t s_buf[IR_SYMS];
static size_t     s_n = IR_SYMS;
static bool       s_armed = false;

static void arm() {
  s_n = IR_SYMS;
  s_armed = rmtReadAsync(IR_RX_PIN, s_buf, &s_n);
}

bool irStart() {
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

void irStop() {
  if (!g_ir.on) return;
  crumb("ir stop");
  if (g_ir.ok) rmtDeinit(IR_RX_PIN);
  s_armed = false;
  g_ir.on = false;
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
  if (n >= 3) irFeed(d, n, now);                             // pass full length so >=70-bit AC remotes are recognized
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
