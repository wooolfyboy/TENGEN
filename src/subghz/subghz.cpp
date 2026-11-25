// ============================================================
//  subghz.cpp — Sub-GHz section on the CC1101
//
//  RECEIVE ONLY: the module is never put into transmit (STX is not used).
//  We measure signal strength (RSSI) at 315 / 433 / 868 / 915 MHz.
//
//  Screens:
//    SPECTRUM — sweep over a ±2 MHz band + waterfall
//    MONITOR  — listen on one frequency and catch bursts (key fobs, bells, sensors)
//    SENSORS  — same + packet parsing: outdoor thermometers, bells (sensors.h)
//  In MONITOR and SENSORS the CC1101 outputs demodulated OOK on GDO0, so sensors
//  are parsed in both (Radio Weather takes the outdoor temperature from here).
// ============================================================
#include "core/settings.h"
#include "core/lang.h"
#include "subghz/subghz.h"
#include "subghz/cc1101.h"
#include "subghz/subghz_model.h"
#include "subghz/sensors.h"
#include "core/timeutil.h"
#include "core/board.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/leds.h"
#include "core/menu.h"
#include "core/psram.h"


int  g_gMode = 0;
bool g_ccOk = false, g_ccTried = false, g_ccOn = false;

int g_band = 1;


// ---------- SPECTRUM ----------
int8_t  g_spCur[SP_N], g_spPeak[SP_N];
uint8_t (*g_wf)[SP_N] = nullptr;
int     g_wfHead = 0, g_wfCount = 0;
int     g_spIdx = 0;
int8_t  g_spRow[SP_N];

float spFreq(int i) { const Band& b = BANDS[g_band]; return b.lo + (b.hi - b.lo) * i / (SP_N - 1); }

static void spReset() {
  if (!g_wf) g_wf = (uint8_t(*)[SP_N])psAlloc<uint8_t>(WF_ROWS * SP_N);
  for (int i = 0; i < SP_N; i++) { g_spCur[i] = -110; g_spPeak[i] = -110; g_spRow[i] = -110; }
  g_wfHead = 0; g_wfCount = 0; g_spIdx = 0;
}

static void spTick() {
  for (int k = 0; k < 6; k++) {                // 6 points per loop pass (~8 ms max stall)
    int r = ccMeasure(spFreq(g_spIdx));
    r = constrain(r, -120, -10);
    g_spRow[g_spIdx] = r;
    g_spCur[g_spIdx] = r;
    if (r > g_spPeak[g_spIdx]) g_spPeak[g_spIdx] = r;
    if (++g_spIdx >= SP_N) {
      g_spIdx = 0;
      for (int i = 0; i < SP_N; i++) {         // waterfall row + peak decay
        if (g_wf) g_wf[g_wfHead][i] = (uint8_t)constrain(g_spRow[i] + 120, 0, 110);
        if (g_spPeak[i] > -110) g_spPeak[i]--; // peaks decay slowly
      }
      g_wfHead = (g_wfHead + 1) % WF_ROWS;
      if (g_wfCount < WF_ROWS) g_wfCount++;
    }
  }
}




// ---------- MONITOR ----------
int8_t   g_mon[MON_N];
int      g_monN = 0;
float    g_floor = -100;          // noise level (slow average)
bool     g_burst = false;
uint32_t g_burstStart = 0, g_burstLastHi = 0;
int      g_burstPeak = -120;
uint32_t g_events = 0;
Burst    g_ev[EV_N];
int      g_evN = 0;

static void monStart() {
  ccStrobe(0x36);
  ccSetBw(0x65);                               // wider (~270 kHz): key fobs and sensors drift in frequency
  ccSetFreq(BANDS[g_band].mon);
  ccStrobe(0x34);                              // SRX
  g_monN = 0; g_burst = false;
}

static void monTick(uint32_t now) {
  int mx = -127;
  for (int k = 0; k < 8; k++) {                // several readings in a row, take the max
    int r = ccRssi();
    if (r > mx) mx = r;
    delayMicroseconds(300);
  }
  if (g_monN < MON_N) g_mon[g_monN++] = mx;
  else { memmove(g_mon, g_mon + 1, MON_N - 1); g_mon[MON_N - 1] = mx; }

  if (!g_burst) {
    g_floor = g_floor * 0.98f + mx * 0.02f;    // noise is averaged outside bursts only
    if (mx > g_floor + BURST_ON_DB) {
      g_burst = true; g_burstStart = now; g_burstLastHi = now; g_burstPeak = mx;
      strip.clear();                            // LED strip flash
      for (int i = 0; i < WS2812_N; i++) strip.setPixelColor(i, strip.Color(40, 30, 0));
      strip.show();
    }
  } else {
    if (mx > g_floor + BURST_OFF_DB) { g_burstLastHi = now; if (mx > g_burstPeak) g_burstPeak = mx; }
    if (now - g_burstLastHi > 150) {           // 150 ms of silence: the burst is over
      g_burst = false;
      g_events++;
      Burst b = { g_burstStart, (uint16_t)min<uint32_t>(g_burstLastHi - g_burstStart, 65000), (int8_t)g_burstPeak };
      if (g_evN < EV_N) g_ev[g_evN++] = b;
      else { memmove(g_ev, g_ev + 1, sizeof(Burst) * (EV_N - 1)); g_ev[EV_N - 1] = b; }
      strip.clear(); strip.show();
    }
  }
}


// ---------- whole section ----------
static void subEnter() {
  if (!g_ccTried) { g_ccTried = true; g_ccOk = ccInit(); spReset(); }
  if (!g_ccOk) return;
  ccAntenna(BANDS[g_band].mon);
  ccStrobe(0x36);
  g_ccOn = true;
}
static void subLeave() { if (g_ccOk) { sensRxStop(); ccSleep(); } g_ccOn = false; ledsOff(); }

// peak level over the last ~10 monitor readings: "strength" of the packet that just ended
static int monRecentPeak() {
  int mx = -120;
  for (int i = max(0, g_monN - 10); i < g_monN; i++) if (g_mon[i] > mx) mx = g_mon[i];
  return mx;
}

static void subStartMode() {
  if (!g_ccOk) return;
  ccAntenna(BANDS[g_band].mon);
  if (g_gMode == G_SPECTRUM) { sensRxStop(); ccSetBw(0xF5); spReset(); }
  else { sensRxStop(); monStart(); sensRxStart(); }
  strip.clear(); strip.show();
}
static void subOpenScreen(int m) { g_gMode = m; subStartMode(); }
static void subCloseScreen() { if (g_ccOk) { sensRxStop(); ccStrobe(0x36); } strip.clear(); strip.show(); }
static int  subScreenNow() { return g_gMode; }
static void subRotate(int dir) {
  static uint32_t last = 0;
  if (millis() - last < 120) return;          // don't skip screens on fast wheel turns
  last = millis();
  g_gMode = (g_gMode + dir + G_NUM) % G_NUM;
  subStartMode();
}
static void subClick() { g_band = (g_band + 1) % N_BANDS; settingsTouch(); subStartMode(); }

// called on every loop() pass while the section screen is open
static void subTick(uint32_t now) {
  if (!g_ccOk || !g_ccOn) return;
  if (g_gMode == G_SPECTRUM) spTick();
  else { monTick(now); sensRxTick(now, monRecentPeak()); }
}


static void subFrame(uint32_t now) {
  if (!g_ccOk) { drawCcMissing(); return; }
  if (g_gMode == G_SPECTRUM) drawSpectrum();
  else if (g_gMode == G_SENSORS) drawSensors(now);
  else drawMonitor(now);
}

static void subScreenInfo(int i, SubItem& it) {
  const char* const names[G_NUM] = { L("Spectrum", "Спектр"), L("Monitor", "Монитор"), L("Sensors", "Датчики") };
  const char* const descs[G_NUM] = { L("band + waterfall", "диапазон и водопад"), L("catch remotes", "ловить пульты"), L("thermometers, bells", "термометры, звонки") };
  it.name = names[i]; it.desc = descs[i];
  if (!g_ccOk && g_ccTried) { strcpy(it.value, L("no chip", "нет чипа")); return; }
  if (i == G_SPECTRUM) strcpy(it.value, BANDS[g_band].name);
  else if (i == G_SENSORS) { if (g_sensN) sprintf(it.value, "%d", g_sensN); else strcpy(it.value, "-"); }
  else if (g_events)   sprintf(it.value, "%lu", (unsigned long)g_events);
  else                 strcpy(it.value, "-");
}

static void subSubtitle(char* out, size_t n) {
  snprintf(out, n, "%s", (g_ccTried && !g_ccOk) ? L("no chip", "нет чипа") : BANDS[g_band].name);
}

static void subTickFn(uint32_t now) { if (!navInSub()) subTick(now); }
static bool subBackFn() { return false; }

const Section subghzSection = {
  "Sub-GHz", 240, 120, 90, iconAntenna, subSubtitle,
  subEnter, subLeave,
  G_NUM, subScreenInfo, subScreenNow, subOpenScreen, subCloseScreen,
  subClick, subRotate, subBackFn, subTickFn, subFrame,
};

// ============================================================
//  for the web page: the stick enables the CC1101 while the Sub-GHz tab
//  on the phone requests data (web.cpp turns it off after a couple of seconds of silence)
// ============================================================
bool subWebUse(int mode, int band) {
  if (!g_ccTried) { g_ccTried = true; g_ccOk = ccInit(); spReset(); }
  if (!g_ccOk) return false;
  mode = constrain(mode, 0, G_NUM - 1); band = constrain(band, 0, N_BANDS - 1);
  bool changed = !g_ccOn || mode != g_gMode || band != g_band;
  g_ccOn = true;
  if (changed) { g_gMode = mode; g_band = band; subStartMode(); }
  return true;
}
void subWebTick(uint32_t now) { subTick(now); }
void subWebStop() { if (g_ccOn) subLeave(); }

void subJson(String& o) {
  uint32_t now = millis();
  const Band& b = BANDS[g_band];
  o.reserve(1200);
  o = "{\"ok\":"; o += g_ccOk ? "true" : "false";
  o += ",\"mode\":"; o += g_gMode;
  o += ",\"band\":"; o += g_band;
  o += ",\"lo\":"; o += String(b.lo, 2); o += ",\"hi\":"; o += String(b.hi, 2); o += ",\"mon\":"; o += String(b.mon, 2);
  if (g_gMode == G_SPECTRUM) {
    o += ",\"cur\":[";
    for (int i = 0; i < SP_N; i++) { if (i) o += ','; o += g_spCur[i]; }
    o += "],\"peak\":[";
    for (int i = 0; i < SP_N; i++) { if (i) o += ','; o += g_spPeak[i]; }
    o += ']';
  } else {
    o += ",\"floor\":"; o += (int)g_floor;
    o += ",\"burst\":"; o += g_burst ? "true" : "false";
    o += ",\"events\":"; o += g_events;
    o += ",\"rssi\":[";
    int from = max(0, g_monN - 120);
    for (int i = from; i < g_monN; i++) { if (i > from) o += ','; o += g_mon[i]; }
    o += "],\"ev\":[";
    for (int i = g_evN - 1; i >= 0; i--) {
      if (i < g_evN - 1) o += ',';
      o += "{\"ago\":"; o += msSince(now, g_ev[i].t) / 1000; o += ",\"ms\":"; o += g_ev[i].durMs; o += ",\"pk\":"; o += g_ev[i].peak; o += '}';
    }
    o += ']';
  }
  o += '}';
}
