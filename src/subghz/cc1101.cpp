// ============================================================
//  cc1101.cpp — minimal CC1101 driver: receive and RSSI only (no TX).
//
//  The CC1101 shares the SPI bus with the display. The display library sets SPI
//  up for writing only, so before each transfer we enable read mode
//  (SET_BUS_READ_MODE); the display restores its own mode on the next draw.
// ============================================================
#include <SPI.h>
#include "subghz/cc1101.h"
#include "core/display.h"

uint8_t g_ccVersion = 0;
static SPISettings g_ccSet(4000000, MSBFIRST, SPI_MODE0);
static SPIClass* g_ccSpi = nullptr;

// ---------- low level ----------
static bool ccSel() {
  digitalWrite(CC_CS, LOW);
  uint32_t t = micros();
  while (digitalRead(CC_MISO)) if (micros() - t > 3000) return false;   // wait until ready
  return true;
}
static void ccBegin() { g_ccSpi->beginTransaction(g_ccSet); SET_BUS_READ_MODE; }
static void ccEnd()   { digitalWrite(CC_CS, HIGH); g_ccSpi->endTransaction(); }

uint8_t ccStrobe(uint8_t s) {
  ccBegin();
  if (!ccSel()) { ccEnd(); return 0xFF; }
  uint8_t st = g_ccSpi->transfer(s);
  ccEnd();
  return st;
}
void ccWrite(uint8_t a, uint8_t v) {
  ccBegin();
  if (!ccSel()) { ccEnd(); return; }
  g_ccSpi->transfer(a); g_ccSpi->transfer(v);
  ccEnd();
}
uint8_t ccReadStatus(uint8_t a) {            // status registers 0x30..0x3D (CC1101 errata SWRZ020E: read until two match)
  ccBegin();
  if (!ccSel()) { ccEnd(); return 0; }
  g_ccSpi->transfer(a | 0xC0);
  uint8_t v = g_ccSpi->transfer(0);
  for (int k = 0; k < 3; k++) {
    g_ccSpi->transfer(a | 0xC0);
    uint8_t v2 = g_ccSpi->transfer(0);
    if (v2 == v) break;
    v = v2;
  }
  ccEnd();
  return v;
}

int ccRssi() {
  uint8_t r = ccReadStatus(0x34);
  int v = (r >= 128) ? ((int)r - 256) / 2 : r / 2;
  return v - 74;
}

void ccAntenna(float mhz) {
  if (mhz < 350)      { digitalWrite(CC_SW1, HIGH); digitalWrite(CC_SW0, LOW);  }
  else if (mhz < 500) { digitalWrite(CC_SW1, HIGH); digitalWrite(CC_SW0, HIGH); }
  else                { digitalWrite(CC_SW1, LOW);  digitalWrite(CC_SW0, HIGH); }
}

void ccSetFreq(float mhz) {
  uint32_t f = (uint32_t)(mhz * 65536.0f / 26.0f + 0.5f);
  ccWrite(0x0D, (f >> 16) & 0xFF);
  ccWrite(0x0E, (f >> 8) & 0xFF);
  ccWrite(0x0F, f & 0xFF);
  // the lower part of each band needs different VCO tuning (as in ELECHOUSE lib)
  bool low = (mhz < 322.88f) || (mhz >= 378 && mhz < 430.5f) || (mhz >= 779 && mhz < 861);
  ccWrite(0x2E, low ? 0x0B : 0x09);          // TEST0
}

// RX bandwidth: 0x?5 in MDMCFG4; F5 ≈ 58 kHz, A5 ≈ 270 kHz
void ccSetBw(uint8_t mdmcfg4) { ccWrite(0x10, mdmcfg4); }

bool ccInit() {
  g_ccSpi = &tft.getSPIinstance();
  pinMode(CC_CS, OUTPUT); digitalWrite(CC_CS, HIGH);
  pinMode(CC_SW1, OUTPUT); pinMode(CC_SW0, OUTPUT);
  ccStrobe(0x30);                              // SRES
  delay(2);
  uint8_t part = ccReadStatus(0x30), ver = ccReadStatus(0x31);
  g_ccVersion = ver;
  if (part != 0x00 || (ver != 0x14 && ver != 0x04 && ver != 0x07)) return false;

  // OOK/ASK receive, no packets: we only need signal level
  static const uint8_t cfg[][2] = {
    {0x00, 0x2E}, {0x01, 0x2E}, {0x02, 0x2E},    // GDO pins high-impedance (MISO stays free)
    {0x03, 0x47}, {0x08, 0x32}, {0x0B, 0x06}, {0x0C, 0x00},
    {0x10, 0xF5}, {0x11, 0x93}, {0x12, 0x30}, {0x13, 0x22}, {0x14, 0xF8},
    {0x15, 0x15}, {0x16, 0x07}, {0x17, 0x30}, {0x18, 0x18},
    {0x19, 0x16}, {0x1A, 0x6C}, {0x1B, 0x07}, {0x1C, 0x00}, {0x1D, 0x91},
    {0x21, 0x56}, {0x22, 0x11},
    {0x23, 0xE9}, {0x24, 0x2A}, {0x25, 0x00}, {0x26, 0x1F},
    {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09},
  };
  for (auto& r : cfg) ccWrite(r[0], r[1]);
  ccStrobe(0x36);                              // SIDLE
  return true;
}

void ccSleep() {
  ccStrobe(0x36);                              // SIDLE
  ccStrobe(0x39);                              // SPWD: power down
}


int ccMeasure(float mhz) {
  ccStrobe(0x36);                              // SIDLE
  ccSetFreq(mhz);
  ccStrobe(0x34);                              // SRX (with auto-calibration)
  uint32_t t = micros();
  while ((ccReadStatus(0x35) & 0x1F) != 0x0D && micros() - t < 2000) {}   // wait for RX state
  delayMicroseconds(250);                      // RSSI settles
  return ccRssi();
}

