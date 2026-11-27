// ============================================================
//  settings.h — persistent settings (stored in NVS)
// ============================================================
#pragma once
#include <Arduino.h>
#include "core/section.h"

// Bluetooth range: a device counts when its smoothed signal >= threshold,
// and drops out only after falling RANGE_HYST_DB below it.
struct RangeOpt { int dbm; const char* name; };
static const RangeOpt RANGES[] = {
  { -127, "all" }, { -90, "far" }, { -80, "room" }, { -70, "near" }, { -60, "very near" },
};
static const int N_RANGES = sizeof(RANGES) / sizeof(RANGES[0]);
static const int RANGE_HYST_DB = 5;
static const uint32_t MIN_PKTS = 2;        // a single stray packet doesn't count

enum { THEME_DENKO = 0, THEME_KURENAI = 1, THEME_N = 2 };                  // themes: DENKŌ 電光 (HUD), KURENAI 紅
extern bool g_screenOff;                   // screen turned off from the tablet (stick still runs); any stick input wakes it
extern int g_lang;                         // UI language: 0 English, 1 Russian (core/lang.h)
extern bool g_langAsk;                     // language not chosen yet (first boot): show the picker
extern int g_theme;                        // theme (THEME_*), set in General
extern int g_range;                        // index into RANGES
extern int g_thresh;                       // LED event threshold (0 = off)
extern int g_ledBright;                    // LED strip brightness, %
extern int g_scrBright;                    // screen brightness, %
extern int g_micEn;                        // microphone enabled (1) / disabled (0)
extern int g_auSrc;
extern int g_city;                         // city for Sky / Globe (index into CITIES, gallery/astro.h)
extern int g_tzOff;                        // local time offset, s (sent by the laptop)
extern int g_auLeds;                       // Sound: LEDs show loudness (off by default)
extern int g_clickEn;                      // wheel clicks
extern int g_bleTicks;                     // closer/farther ticks in Bluetooth
extern int g_wifiTicks;                    // closer/farther ticks in Wi-Fi Find
extern int g_wRange;                       // Wi-Fi: hide networks weaker than this (index into RANGES)
extern int g_sndIdx;                       // sounds: 0 off, 1 quiet, 2 loud
extern int g_dimIdx;                       // auto-dim: index into DIM_SECS (0 = off)
extern int g_emVol;                        // emergency shutdown siren volume (0..100%)
extern int g_emLed;                        // emergency shutdown LED brightness (0..100%)
extern int g_emSec;                        // emergency shutdown countdown time (1..30 s)
static const uint16_t DIM_SECS[] = { 0, 30, 60, 300 };
static const int N_DIMS = sizeof(DIM_SECS) / sizeof(DIM_SECS[0]);

void settingsBegin();                      // load and apply
void applyBrightness();
void settingsAutosave(uint32_t now);       // save 3 s after a change
void settingsSaveNow();
void settingsSetLang(int lang);           // first-boot language choice (saved immediately)
void settingsTouch();                      // mark settings as changed (saved later)
void settingsJson(String& o);              // for the tablet: all settings (/api/settings)
bool settingsSetKey(const char* key, const char* val, String& o);   // set an item by key (/api/set); o = reply

extern const Section settingsSection;
