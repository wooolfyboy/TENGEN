// ============================================================
//  sens_rx.cpp — 433 MHz packet reception for sensors (receive only).
//
//  In async OOK mode the CC1101 outputs the demodulated signal on GDO0 (GPIO 3):
//  1 = carrier, 0 = silence. An edge interrupt pushes durations into a ring buffer;
//  the main loop assembles a packet (pulse + gap) and, after 20 ms of silence,
//  hands it to sensFeed (parsed in ook.cpp).
//
//  No transmit: GDO0 is an input, STX is never issued.
// ============================================================
#include <soc/gpio_reg.h>
#include "subghz/sensors.h"
#include "subghz/cc1101.h"
#include "core/crumb.h"

#define CC_GDO0 3                          // BOARD_LORA_IO0 (official LilyGO pinout)
#define RING 2048                          // edges in the queue (~0.5 s of a dense packet)
#define TRAIN_END_US 20000                 // silence longer than this ends the packet
#define GLITCH_US 40                       // pulses and gaps shorter than this are glitches, merged

static volatile uint32_t s_ring[RING];     // bit 31 = level of the finished segment, rest = µs
static volatile uint16_t s_w = 0, s_r = 0;
static volatile uint32_t s_lastEdge = 0;
static volatile bool     s_over = false;
static bool s_on = false;

static void IRAM_ATTR gdoIsr() {
  uint32_t t = micros();
  uint32_t lvl = (REG_READ(GPIO_IN_REG) >> CC_GDO0) & 1;   // new level
  uint32_t d = t - s_lastEdge;
  s_lastEdge = t;
  uint16_t w = s_w, nw = (w + 1) & (RING - 1);
  if (nw == s_r) { s_over = true; return; }
  s_ring[w] = (d & 0x7FFFFFFF) | (lvl ? 0 : 0x80000000u);   // the finished segment was of the opposite level
  s_w = nw;
}

// ---------- packet assembly ----------
static OokTrain s_train;
static bool     s_havePulse = false;        // pulse recorded, waiting for the gap after it
static uint16_t s_pulse = 0;
static bool     s_full = false;             // packet cut by a full buffer (not by silence)

static void cleanup(OokTrain& T) {          // merge glitches shorter than GLITCH_US
  int o = 0;
  for (int i = 0; i < T.n; i++) {
    if (T.p[i] < GLITCH_US && o > 0) {       // short pulse: part of the gap
      uint32_t g = (uint32_t)T.g[o - 1] + T.p[i] + T.g[i];
      T.g[o - 1] = g > 65535 ? 65535 : g;
      continue;
    }
    T.p[o] = T.p[i]; T.g[o] = T.g[i];
    if (T.g[o] < GLITCH_US && i + 1 < T.n) { // short gap: part of the pulse
      uint32_t p = (uint32_t)T.p[o] + T.g[o] + T.p[i + 1];
      T.p[o] = p > 65535 ? 65535 : p; T.g[o] = T.g[i + 1];
      i++;
    }
    o++;
  }
  T.n = o;
}

static int s_rssi = -120;
static void finish(uint32_t now) {
  if (s_train.n >= 8) {
    cleanup(s_train);
    crumb("sens feed");
    if (s_full) g_sensNoise++;               // continuous stream with no silence: air noise, not a packet
    else sensFeed(s_train, s_rssi, now);
  }
  s_train.n = 0; s_havePulse = false; s_full = false;
  s_rssi = -120;
}

static void segment(bool high, uint32_t us, uint32_t now) {
  uint16_t d = us > 65535 ? 65535 : (uint16_t)us;
  if (high) { s_pulse = d; s_havePulse = true; return; }
  if (!s_havePulse) return;                  // silence before the first pulse
  s_havePulse = false;
  if (s_train.n < OOK_MAXP) { s_train.p[s_train.n] = s_pulse; s_train.g[s_train.n] = d; s_train.n++; }
  if (us > TRAIN_END_US) finish(now);
  else if (s_train.n >= OOK_MAXP) { s_full = true; finish(now); }
}

void sensRxStart() {
  if (s_on) return;
  pinMode(CC_GDO0, INPUT);
  ccStrobe(0x36);                            // SIDLE: change settings outside of RX
  ccWrite(0x02, 0x0D);                       // IOCFG0: GDO0 = async data (demodulated OOK)
  ccWrite(0x08, 0x32);                       // PKTCTRL0: async serial mode, no packets
  ccWrite(0x12, 0x30);                       // MDMCFG2: ASK/OOK, no sync word
  ccWrite(0x1B, 0x07); ccWrite(0x1C, 0x00); ccWrite(0x1D, 0x91);   // AGC for OOK (42 dB threshold, no freeze)
  ccStrobe(0x34);                            // SRX: back to receive
  s_train.n = 0; s_havePulse = false; s_full = false;
  s_r = s_w; s_over = false;
  s_lastEdge = micros();
  attachInterrupt(digitalPinToInterrupt(CC_GDO0), gdoIsr, CHANGE);
  s_on = true;
}

void sensRxStop() {
  if (!s_on) return;
  detachInterrupt(digitalPinToInterrupt(CC_GDO0));
  ccWrite(0x02, 0x2E);                       // GDO0 back to high impedance
  s_on = false;
  s_train.n = 0; s_havePulse = false;
}

bool sensRxOn() { return s_on; }

void sensRxTick(uint32_t now, int rssi) {
  if (!s_on) return;
  if (rssi > s_rssi || s_train.n == 0) s_rssi = rssi;          // peak per packet
  if (s_over) { s_over = false; s_r = s_w; s_train.n = 0; s_havePulse = false; g_sensNoise++; return; }
  int guard = RING;
  while (s_r != s_w && guard--) {
    uint32_t v = s_ring[s_r];
    s_r = (s_r + 1) & (RING - 1);
    segment(v & 0x80000000u, v & 0x7FFFFFFF, now);
  }
  // silence after the last pulse: close the packet by time (there may be no next edge)
  if (s_havePulse || s_train.n) {
    uint32_t last = s_lastEdge;                                  // sample volatile timestamp before micros()
    int32_t quiet = (int32_t)(micros() - last);                  // signed delta avoids uint32 underflow if ISR fires in between
    bool low = !((REG_READ(GPIO_IN_REG) >> CC_GDO0) & 1);
    if (low && quiet > (int32_t)TRAIN_END_US) {
      if (s_havePulse) segment(false, 65535, now);
      else finish(now);
    }
  }
}
