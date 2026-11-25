// ============================================================
//  battery.cpp — battery state, read-only (no configuration):
//   BQ27220 (0x55) fuel gauge: %, voltage, current, time; BQ25896 (0x6B) charger: USB present, charging.
//  I2C: SDA 8, SCL 18 (LilyGO utilities.h).
// ============================================================
#include <Wire.h>
#include "core/lang.h"
#include "core/timeutil.h"
#include "core/battery.h"
#include "core/display.h"
#include "core/theme.h"

#define BAT_I2C_SDA 8
#define BAT_I2C_SCL 18
#define BQ27220_ADDR 0x55
#define BQ25896_ADDR 0x6B

Bat g_bat;

static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static bool bq16(uint8_t reg, uint16_t& v) {
  uint8_t b[2];
  if (!i2cRead(BQ27220_ADDR, reg, b, 2)) return false;
  v = b[0] | (b[1] << 8);
  return true;
}

static void batRead() {
  uint16_t v;
  g_bat.ok = bq16(0x2C, v);                     // StateOfCharge
  if (g_bat.ok) {
    g_bat.soc = constrain((int)v, 0, 100);
    if (bq16(0x08, v)) g_bat.mv = v;            // Voltage
    if (bq16(0x0C, v)) g_bat.ma = (int16_t)v;   // Current
    if (bq16(0x06, v)) g_bat.tempC = v / 10.0f - 273.15f;
    if (bq16(0x16, v)) g_bat.tte = (v == 0xFFFF) ? -1 : v;   // TimeToEmpty
    if (bq16(0x18, v)) g_bat.ttf = (v == 0xFFFF) ? -1 : v;   // TimeToFull
    if (bq16(0x10, v)) g_bat.remain = v;
    if (bq16(0x12, v)) g_bat.full = v;
  }
  uint8_t st;
  g_bat.chgOk = i2cRead(BQ25896_ADDR, 0x0B, &st, 1);       // REG0B: status
  if (g_bat.chgOk) {
    g_bat.bus = (st >> 5) & 0x07;
    g_bat.chg = (st >> 3) & 0x03;
  }
}

void batBegin() {
  Wire.begin(BAT_I2C_SDA, BAT_I2C_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(50);
  batRead();
  g_bat.t = millis();
}

void batTick(uint32_t now) {
  if (msSince(now, g_bat.t) < 2000) return;
  g_bat.t = now;
  batRead();
}

bool batUsb()      { return g_bat.chgOk && g_bat.bus != 0; }
bool batCharging() { return g_bat.chgOk && (g_bat.chg == 1 || g_bat.chg == 2); }

uint16_t batColor() {
  if (batCharging())  return C(90, 200, 255);
  if (g_bat.soc > 50) return C(70, 210, 130);
  if (g_bat.soc > 20) return C(240, 200, 70);
  return C(235, 80, 80);
}


// one-line power summary (Settings)
void batSummary(char* out, size_t n) {
  if (!g_bat.ok) { snprintf(out, n, L("battery gauge not found", "датчик батареи не найден")); return; }
  if (batCharging()) {
    if (g_bat.ttf > 0) snprintf(out, n, L("charging %d mA, full in ~%dh %02dm", "зарядка %d мА, полностью через ~%d ч %02d мин"), g_bat.ma, g_bat.ttf / 60, g_bat.ttf % 60);
    else               snprintf(out, n, L("charging %d mA", "зарядка %d мА"), g_bat.ma);
  } else if (batUsb()) {
    snprintf(out, n, "%s", g_bat.chg == 3 ? L("USB power, fully charged", "питание USB, заряжено") : L("USB power", "питание USB"));
  } else if (g_bat.tte > 0) {
    snprintf(out, n, L("on battery %d mA, ~%dh %02dm left", "от батареи %d мА, осталось ~%d ч %02d мин"), g_bat.ma, g_bat.tte / 60, g_bat.tte % 60);
  } else {
    snprintf(out, n, L("on battery %d mA", "от батареи %d мА"), g_bat.ma);
  }
}
