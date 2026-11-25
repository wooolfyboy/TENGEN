// ============================================================
//  settings.cpp — settings storage and the settings screen
// ============================================================
#include <Preferences.h>
#include "core/lang.h"
#include "core/timeutil.h"
#include "core/settings.h"
#include "core/version.h"
#include "core/crumb.h"
#include "core/jsonutil.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/leds.h"
#include "core/battery.h"
#include "core/ui.h"
#include "audio/audio.h"
#include "subghz/subghz_model.h"
#include "gallery/astro.h"

int g_range = 0;
int g_thresh = 7;
int g_ledBright = 50;
int g_scrBright = 100;
int g_dimIdx = 0;                        // screen never dims by default
int g_sndIdx = 1;                        // quiet UI sounds
int g_clickEn = 1;
int g_theme = THEME_DENKO;                // theme: DENKO 電光 (HUD) by default
int g_micEn = 1;
int g_auSrc = 0;
int g_city = 0;
int g_lang = 0;                          // UI language: 0 English, 1 Russian (lang.h)
bool g_langAsk = false;                  // language not chosen yet: ask at startup (core/langpick)
int g_tzOff = 10800;
int g_auLeds = 0;
int g_bleTicks = 1;
int g_wifiTicks = 1;
int g_wRange = 0;
// alerts (HQ on tablet / PC): what counts as near, new, alarm, and how airwave activity is scored
int g_alNear = -65, g_alNew = 1, g_alTrk = 1, g_alNewFor = 30, g_alWarm = 20, g_alSens = 5,
    g_alWarn = 60, g_alAlarm = 80, g_alCBle = 1, g_alCRf = 1, g_alCSnd = 1, g_alCPrb = 1, g_alBlink = 1, g_alPlate = 1;

static int      g_setPage = 0;           // page: general / section
static int      g_setItem = 0;           // selected item on the page
static bool     g_setDirty = false;
static uint32_t g_setChangedMs = 0;
static Preferences g_prefs;

// All settings; page = which page shows the item.
// One table for everything: the stick screen (wheel) and the tablet (/api/settings,
// /api/set) both go through getVal / setVal, so the logic isn't duplicated.
enum { S_THEME, S_LANG, S_SCREEN, S_DIM, S_LEDBR, S_VOLUME, S_CLICKS, S_CITY, S_BATTERY, S_VERSION,
       S_RANGE, S_LEDEV, S_BLETICK,
       S_WRANGE, S_WTICK,
       S_BAND,
       S_MIC, S_AULED, S_AUSRC,
       S_ALNEAR, S_ALNEW, S_ALTRK, S_ALNEWFOR, S_ALWARM, S_ALSENS, S_ALWARN, S_ALALARM,
       S_ALCBLE, S_ALCRF, S_ALCSND, S_ALCPRB, S_ALBLINK, S_ALPLATE, S_NUM };
enum { P_GENERAL, P_BLE, P_WIFI, P_SUBGHZ, P_SOUND, P_ALERT, P_NUM };
enum { K_ENUM, K_RANGE, K_BOOL, K_INFO };                 // item kind (for the tablet)
struct SetDef { const char* name; const char* hint; uint8_t page; const char* key; uint8_t kind; int16_t mn, mx, step; const char* unit; };
static const SetDef DEFS[S_NUM] = {
  { LP("Theme", "Тема"), LP("look: denko neon / kurenai red", "оформление: denko / kurenai"),   P_GENERAL, "theme",      K_ENUM },
  { LP("Language", "Язык"), LP("interface: English / Русский", "интерфейс: English / Русский"), P_GENERAL, "lang",   K_ENUM },
  { LP("Screen", "Экран"), LP("screen backlight", "яркость подсветки"),            P_GENERAL, "screen",     K_RANGE, 20, 100, 10, "%" },
  { LP("Auto dim", "Затемнение"), LP("dim screen when idle", "гасить экран без действий"),        P_GENERAL, "dim",        K_ENUM },
  { LP("LED brightness", "Подсветка"), LP("LED strip brightness", "яркость светодиодов"),        P_GENERAL, "ledbr",      K_RANGE, 10, 100, 10, "%" },
  { LP("Volume", "Звук"), LP("all sounds: off / quiet / loud", "все звуки: выкл / тихо / громко"),  P_GENERAL, "volume",     K_ENUM },
  { LP("Wheel clicks", "Щелчки колеса"), LP("click on every wheel step", "щелчок на каждый шаг колеса"),   P_GENERAL, "clicks",     K_BOOL },
  { LP("City", "Город"), LP("for the sky map and satellites", "для карты неба и спутников"),  P_GENERAL, "city",       K_ENUM },
  { LP("Battery", "Батарея"), "",                            P_GENERAL, "battery",    K_INFO },
  { LP("Version", "Версия"), LP("TENGEN firmware", "прошивка TENGEN"),             P_GENERAL, "version",    K_INFO },
  { LP("Range", "Дальность"), LP("ignore weaker devices", "не считать слабые устройства"),       P_BLE,     "ble_range",  K_ENUM },
  { LP("LED events", "Вспышки"), LP("LEDs flash when the count jumps", "светодиоды мигают при скачке"),    P_BLE,     "ble_ledev",  K_RANGE, 0, 30, 1, "" },
  { LP("Finder ticks", "Тики поиска"), LP("ticks on the nearest screen", "тики на экране «ближайшее»"),     P_BLE,     "ble_ticks",  K_BOOL },
  { LP("Range", "Дальность"), LP("hide weaker networks", "скрывать слабые сети"),        P_WIFI,    "wifi_range", K_ENUM },
  { LP("Finder ticks", "Тики поиска"), LP("ticks on the finder screen", "тики на экране поиска"),        P_WIFI,    "wifi_ticks", K_BOOL },
  { LP("Start band", "Начальный диапазон"), LP("band when the section opens", "диапазон при открытии раздела"),     P_SUBGHZ,  "sub_band",   K_ENUM },
  { LP("Microphone", "Микрофон"), LP("microphone for the Sound section", "микрофон для раздела «Звук»"),       P_SOUND,   "mic",        K_BOOL },
  { LP("LED meter", "Индикатор"), LP("LEDs show loudness", "светодиоды показывают громкость"),          P_SOUND,   "au_leds",    K_BOOL },
  { LP("Source", "Источник"), LP("auto / microphone / computer audio", "авто / микрофон / звук ПК"),   P_SOUND,   "au_src",     K_ENUM },
  { LP("Near", "Рядом"), LP("stronger than this counts as near", "сильнее этого - «рядом»"),   P_ALERT,   "al_near",    K_RANGE, -90, -40, 5, "dBm" },
  { LP("New device", "Новое устройство"), LP("alert: off / near / any", "тревога: выкл / рядом / все"),   P_ALERT,   "al_new",     K_ENUM },
  { LP("Tracker", "Трекер"), LP("tracker alert: off / near / any", "трекеры: выкл / рядом / все"), P_ALERT,   "al_trk",     K_ENUM },
  { LP("New for", "Новое в течение"), LP("how long a device counts as new", "сколько устройство считается новым"),    P_ALERT,   "al_newfor",  K_RANGE, 10, 300, 10, "s" },
  { LP("Warm-up", "Разогрев"), LP("right after connecting nothing is new", "после подключения новых нет"),P_ALERT,   "al_warm",    K_RANGE, 0, 120, 5, "s" },
  { LP("Sensitivity", "Чувствительность"), LP("activity level gain", "усиление шкалы активности"),         P_ALERT,   "al_sens",    K_RANGE, 1, 10, 1, "" },
  { LP("Attention at", "Внимание с"), LP("activity level for attention", "уровень активности «внимание»"),   P_ALERT,   "al_warn",    K_RANGE, 20, 95, 5, "%" },
  { LP("Alarm at", "Тревога с"), LP("activity level for alarm", "уровень активности «тревога»"),       P_ALERT,   "al_alarm",   K_RANGE, 30, 100, 5, "%" },
  { LP("Count Bluetooth", "Учитывать BLE"), LP("new devices raise activity", "новые устройства"),  P_ALERT,   "al_cble",    K_BOOL },
  { LP("Count 433 MHz", "Учитывать 433"), LP("433 MHz packets raise activity", "посылки 433 МГц"),  P_ALERT,   "al_crf",     K_BOOL },
  { LP("Count sound", "Учитывать звук"), LP("loudness raises activity", "громкость звука"),    P_ALERT,   "al_csnd",    K_BOOL },
  { LP("Count IR/NFC", "Учитывать ИК/NFC"), LP("remotes and cards raise activity", "пульты и карты"),P_ALERT,   "al_cprb",    K_BOOL },
  { LP("Blinking", "Мигание"), LP("blink on alerts", "мигать при тревоге"),             P_ALERT,   "al_blink",   K_BOOL },
  { LP("Alert banner", "Плашка тревоги"), LP("show the proximity warning", "показывать плашку тревоги"),      P_ALERT,   "al_plate",   K_BOOL },
};
static const char* const PAGE_NAMES[P_NUM] = { LP("General", "Общие"), "Bluetooth", "Wi-Fi", "Sub-GHz", LP("Sound", "Звук"), LP("Alerts", "Тревоги") };
static const char* const PAGE_IDS[P_NUM]   = { "general", "ble", "wifi", "sub", "snd", "alert" };
static const char* const PAGE_DESCS[P_NUM] = { LP("theme, screen, LEDs, city", "тема, экран, город"), LP("range, LEDs, ticks", "дальность, вспышки"),
                                               LP("range, ticks", "дальность, тики"), LP("radio band", "диапазон"), LP("source, mic, LEDs", "источник, микрофон"), LP("near, new, levels", "рядом, новые, уровни") };
static const int SET_ROWS = 7;           // rows on screen (list scrolls)

// items of the current page
static int pageItems(int page, int* out) {
  int n = 0;
  for (int i = 0; i < S_NUM; i++) if (DEFS[i].page == page) out[n++] = i;
  return n;
}

// ---------- settings ----------
bool g_screenOff = false;
void applyBrightness() {
  static bool s_wasOff = false;
  strip.setBrightness(g_ledBright * 255 / 100);
  if (g_screenOff != s_wasOff) {
    s_wasOff = g_screenOff;
    tft.writecommand(g_screenOff ? 0x28 : 0x29);   // ST7789 display off / on
    if (!g_screenOff) themeChanged();              // force clean full redraw on wake
  }
  analogWrite(TFT_BL, g_screenOff ? 0 : g_scrBright * 255 / 100);
}

void loadSettings() {
  g_prefs.begin("radar", true);
  g_range     = g_prefs.getInt("range", 0);
  g_thresh    = g_prefs.getInt("thresh", 7);
  g_ledBright = g_prefs.getInt("ledbr", 50);
  g_scrBright = g_prefs.getInt("scrbr", 100);
  g_dimIdx    = g_prefs.getInt("dim", 0);
  g_sndIdx    = g_prefs.getInt("snd", 1);
  g_micEn     = g_prefs.getInt("mic", 1) ? 1 : 0;
  g_clickEn   = g_prefs.getInt("click", 1) ? 1 : 0;
  g_theme     = constrain(g_prefs.getInt("th2", THEME_DENKO), 0, THEME_N - 1);
  { int lg = g_prefs.getInt("langv", -1);            // "langv" is written only by a real language choice (picker / Settings)
    g_langAsk = lg < 0;                                 // absent: show the picker (also once after updating from 2.0-test1..4,
    if (lg < 0) lg = g_prefs.getInt("lang", 0);         //   where "lang" may have been written by autosave); old "lang" is only a hint
    g_lang = lg == 1 ? 1 : 0; }
  g_auLeds    = g_prefs.getInt("auled", 0) ? 1 : 0;
  g_auSrc     = constrain(g_prefs.getInt("ausrc", 0), 0, 2);
  g_gpsKnown  = g_prefs.getBool("gpsok", false);
  g_gpsCity.lat = g_prefs.getFloat("gpslat", 0); g_gpsCity.lon = g_prefs.getFloat("gpslon", 0);
  g_city      = constrain(g_prefs.getInt("city", 0), 0, cityCount() - 1);
  g_tzOff     = g_prefs.getInt("tz", 10800);
  g_bleTicks  = g_prefs.getInt("bltick", 1) ? 1 : 0;
  g_wifiTicks = g_prefs.getInt("wftick", 1) ? 1 : 0;
  g_wRange    = g_prefs.getInt("wrange", 0);
  g_band      = g_prefs.getInt("band", 1);
  g_alNear  = constrain(g_prefs.getInt("alnear", -65), -90, -40);
  g_alNew   = constrain(g_prefs.getInt("alnew", 1), 0, 2);
  g_alTrk   = constrain(g_prefs.getInt("altrk", 1), 0, 2);
  g_alNewFor= constrain(g_prefs.getInt("alnewf", 30), 10, 300);
  g_alWarm  = constrain(g_prefs.getInt("alwarm", 20), 0, 120);
  g_alSens  = constrain(g_prefs.getInt("alsens", 5), 1, 10);
  g_alWarn  = constrain(g_prefs.getInt("alwarn", 60), 20, 95);
  g_alAlarm = constrain(g_prefs.getInt("alalarm", 80), 30, 100);
  if (g_alAlarm <= g_alWarn) g_alAlarm = min(100, g_alWarn + 5);
  g_alCBle  = g_prefs.getInt("alcble", 1) ? 1 : 0;
  g_alCRf   = g_prefs.getInt("alcrf", 1) ? 1 : 0;
  g_alCSnd  = g_prefs.getInt("alcsnd", 1) ? 1 : 0;
  g_alCPrb  = g_prefs.getInt("alcprb", 1) ? 1 : 0;
  g_alBlink = g_prefs.getInt("alblink", 1) ? 1 : 0;
  g_alPlate = g_prefs.getInt("alplate", 1) ? 1 : 0;
  g_prefs.end();
  if (g_range < 0 || g_range >= N_RANGES) g_range = 0;
  if (g_thresh < 0 || g_thresh > 30) g_thresh = 7;
  if (g_ledBright < 10 || g_ledBright > 100) g_ledBright = 50;
  if (g_scrBright < 20 || g_scrBright > 100) g_scrBright = 100;
  if (g_dimIdx < 0 || g_dimIdx >= N_DIMS) g_dimIdx = 0;
  if (g_sndIdx < 0 || g_sndIdx > 2) g_sndIdx = 1;
  if (g_wRange < 0 || g_wRange >= N_RANGES) g_wRange = 0;
  if (g_band < 0 || g_band >= N_BANDS) g_band = 1;
}

void saveSettings() {
  g_prefs.begin("radar", false);
  g_prefs.putInt("range", g_range);
  g_prefs.putInt("thresh", g_thresh);
  g_prefs.putInt("ledbr", g_ledBright);
  g_prefs.putInt("scrbr", g_scrBright);
  g_prefs.putInt("dim", g_dimIdx);
  g_prefs.putInt("snd", g_sndIdx);
  g_prefs.putInt("mic", g_micEn);
  g_prefs.putInt("click", g_clickEn);
  g_prefs.putInt("th2", g_theme);
  if (!g_langAsk) { g_prefs.putInt("langv", g_lang); g_prefs.putInt("lang", g_lang); }   // don't write until a language is chosen, or autosave (PC time, GPS) would skip the picker
  g_prefs.putInt("auled", g_auLeds);
  g_prefs.putInt("ausrc", g_auSrc);
  g_prefs.putInt("city", g_city);
  g_prefs.putBool("gpsok", g_gpsKnown); g_prefs.putFloat("gpslat", g_gpsCity.lat); g_prefs.putFloat("gpslon", g_gpsCity.lon);
  g_prefs.putInt("tz", g_tzOff);
  g_prefs.putInt("bltick", g_bleTicks);
  g_prefs.putInt("wftick", g_wifiTicks);
  g_prefs.putInt("wrange", g_wRange);
  g_prefs.putInt("band", g_band);
  g_prefs.putInt("alnear", g_alNear);   g_prefs.putInt("alnew", g_alNew);     g_prefs.putInt("altrk", g_alTrk);
  g_prefs.putInt("alnewf", g_alNewFor); g_prefs.putInt("alwarm", g_alWarm);   g_prefs.putInt("alsens", g_alSens);
  g_prefs.putInt("alwarn", g_alWarn);   g_prefs.putInt("alalarm", g_alAlarm);
  g_prefs.putInt("alcble", g_alCBle);   g_prefs.putInt("alcrf", g_alCRf);     g_prefs.putInt("alcsnd", g_alCSnd);
  g_prefs.putInt("alcprb", g_alCPrb);   g_prefs.putInt("alblink", g_alBlink); g_prefs.putInt("alplate", g_alPlate);
  g_prefs.end();
  g_setDirty = false;
}

void settingsSaveNow() { if (g_setDirty) saveSettings(); }
void settingsSetLang(int lang) { g_lang = lang ? 1 : 0; g_langAsk = false; saveSettings(); }
void settingsAutosave(uint32_t now) { if (g_setDirty && msSince(now, g_setChangedMs) > 3000) saveSettings(); }
void settingsBegin() { loadSettings(); applyBrightness(); }

static int curItem() {
  int ids[S_NUM]; int n = pageItems(g_setPage, ids);
  return n ? ids[constrain(g_setItem, 0, n - 1)] : -1;
}

// ---------- item values: one logic for the wheel and the tablet ----------
static const char* const VOL_NAMES[3] = { "off", "quiet", "loud" };
static const char* const SRC_NAMES[3] = { "auto", "mic", "laptop" };
static const char* const AL_NAMES[3]  = { "off", "near", "any" };

static int optCount(int id) {                             // number of options of a list item
  switch (id) {
    case S_THEME: return THEME_N;
    case S_LANG: return 2;
    case S_DIM: return N_DIMS;
    case S_VOLUME: return 3;
    case S_CITY: return cityCount();
    case S_RANGE: case S_WRANGE: return N_RANGES;
    case S_BAND: return N_BANDS;
    case S_AUSRC: return 3;
    case S_ALNEW: case S_ALTRK: return 3;
  }
  return 0;
}
static void optName(int id, int i, char* out, size_t n) {   // option name (as on the tablet)
  switch (id) {
    case S_THEME:  snprintf(out, n, "%s", i == THEME_KURENAI ? "kurenai" : "denko"); break;
    case S_LANG:   snprintf(out, n, "%s", i ? "ru" : "en"); break;
    case S_DIM:    if (DIM_SECS[i] == 0) snprintf(out, n, "off"); else if (DIM_SECS[i] < 60) snprintf(out, n, "%d s", DIM_SECS[i]); else snprintf(out, n, "%d min", DIM_SECS[i] / 60); break;
    case S_VOLUME: snprintf(out, n, "%s", VOL_NAMES[i]); break;
    case S_CITY:   snprintf(out, n, "%s", i == N_CITIES ? g_gpsCity.name : LEN(CITIES[i].name)); break;
    case S_RANGE: case S_WRANGE: snprintf(out, n, "%s", RANGES[i].name); break;
    case S_BAND:   snprintf(out, n, "%s", BANDS[i].name); break;
    case S_AUSRC:  snprintf(out, n, "%s", SRC_NAMES[i]); break;
    case S_ALNEW: case S_ALTRK: snprintf(out, n, "%s", AL_NAMES[constrain(i, 0, 2)]); break;
    default: out[0] = 0;
  }
}
// human-readable option label (device screen, "labels" for the tablet); optName is the machine name for /api/set
static void optLabel(int id, int i, char* out, size_t n) {
  optName(id, i, out, n);
  if (id == S_CITY) { snprintf(out, n, "%s", i == N_CITIES ? g_gpsCity.name : LT(CITIES[i].name)); return; }
  if (id == S_LANG) { snprintf(out, n, "%s", i ? "Русский" : "English"); return; }
  if (g_lang) {
    static const char* const MAP[][2] = {                 // machine names -> labels (strings are language-independent, so static is fine)
      { "off", "выкл" }, { "quiet", "тихо" }, { "loud", "громко" }, { "auto", "авто" }, { "mic", "микрофон" },
      { "laptop", "ПК" }, { "near", "рядом" }, { "any", "все" }, { "all", "все" }, { "far", "далеко" },
      { "room", "комната" }, { "very near", "вплотную" },
    };
    for (auto& m : MAP) if (!strcmp(out, m[0])) { snprintf(out, n, "%s", m[1]); return; }
    if (id == S_DIM) {
      if (DIM_SECS[i] >= 60) snprintf(out, n, "%d мин", DIM_SECS[i] / 60);
      else if (DIM_SECS[i] > 0) snprintf(out, n, "%d с", DIM_SECS[i]);
    }
  } else if (id == S_AUSRC && !strcmp(out, "laptop")) snprintf(out, n, "computer");
}
static int getVal(int id) {                               // option index / number / 0-1
  switch (id) {
    case S_THEME: return g_theme;          case S_LANG: return g_lang;          case S_SCREEN: return g_scrBright;   case S_DIM: return g_dimIdx;
    case S_LEDBR: return g_ledBright;      case S_VOLUME: return g_sndIdx;      case S_CLICKS: return g_clickEn;
    case S_CITY: return g_city;            case S_RANGE: return g_range;        case S_LEDEV: return g_thresh;
    case S_BLETICK: return g_bleTicks;     case S_WRANGE: return g_wRange;      case S_WTICK: return g_wifiTicks;
    case S_BAND: return g_band;            case S_MIC: return g_micEn;          case S_AULED: return g_auLeds;
    case S_AUSRC: return g_auSrc;
    case S_ALNEAR: return g_alNear;        case S_ALNEW: return g_alNew;        case S_ALTRK: return g_alTrk;
    case S_ALNEWFOR: return g_alNewFor;    case S_ALWARM: return g_alWarm;      case S_ALSENS: return g_alSens;
    case S_ALWARN: return g_alWarn;        case S_ALALARM: return g_alAlarm;
    case S_ALCBLE: return g_alCBle;        case S_ALCRF: return g_alCRf;        case S_ALCSND: return g_alCSnd;
    case S_ALCPRB: return g_alCPrb;        case S_ALBLINK: return g_alBlink;    case S_ALPLATE: return g_alPlate;
  }
  return 0;
}
// set a value: bounds check and the same side effects as a wheel turn
static bool setVal(int id, int v) {
  const SetDef& d = DEFS[id];
  if (d.kind == K_INFO) return false;
  if (d.kind == K_ENUM)  v = constrain(v, 0, optCount(id) - 1);
  if (d.kind == K_BOOL)  v = v ? 1 : 0;
  if (d.kind == K_RANGE) v = constrain(v, d.mn, d.mx);
  switch (id) {
    case S_THEME:   g_theme = v; themeChanged(); break;
    case S_LANG:    g_lang = v; g_langAsk = false; break;      // texts are fetched on every redraw, so it applies immediately
    case S_SCREEN:  g_scrBright = v; break;
    case S_DIM:     g_dimIdx = v; break;
    case S_LEDBR:   g_ledBright = v; break;
    case S_VOLUME:  g_sndIdx = v; audioBeep(1400, 60); break;
    case S_CLICKS:  g_clickEn = v; break;
    case S_CITY:    g_city = v; break;
    case S_RANGE:   g_range = v; break;
    case S_LEDEV:   g_thresh = v; break;
    case S_BLETICK: g_bleTicks = v; break;
    case S_WRANGE:  g_wRange = v; break;
    case S_WTICK:   g_wifiTicks = v; break;
    case S_BAND:    g_band = v; break;
    case S_MIC:     g_micEn = v; break;
    case S_AULED:   g_auLeds = v; if (!g_auLeds) ledsOff(); break;
    case S_AUSRC:   g_auSrc = v; break;
    case S_ALNEAR:  g_alNear = v; break;
    case S_ALNEW:   g_alNew = v; break;
    case S_ALTRK:   g_alTrk = v; break;
    case S_ALNEWFOR:g_alNewFor = v; break;
    case S_ALWARM:  g_alWarm = v; break;
    case S_ALSENS:  g_alSens = v; break;
    case S_ALWARN:  g_alWarn = v; if (g_alAlarm <= v) g_alAlarm = min(100, v + 5); break;          // alarm is always above attention
    case S_ALALARM: g_alAlarm = v; if (g_alWarn >= v) g_alWarn = max(20, v - 5); break;
    case S_ALCBLE:  g_alCBle = v; break;
    case S_ALCRF:   g_alCRf = v; break;
    case S_ALCSND:  g_alCSnd = v; break;
    case S_ALCPRB:  g_alCPrb = v; break;
    case S_ALBLINK: g_alBlink = v; break;
    case S_ALPLATE: g_alPlate = v; break;
    default: return false;
  }
  applyBrightness();
  if (id == S_LEDBR) ledsEvent(+1);        // preview strip brightness
  settingsTouch();
  return true;
}

void settingsAdjust(int dir) {
  int id = curItem();
  if (id < 0) return;
  const SetDef& d = DEFS[id];
  switch (d.kind) {
    case K_ENUM:
      if (id == S_CITY) setVal(id, (getVal(id) + dir + optCount(id)) % optCount(id));   // cities wrap around
      else setVal(id, getVal(id) + dir);
      break;
    case K_BOOL:  setVal(id, dir > 0); break;
    case K_RANGE: setVal(id, getVal(id) + dir * d.step); break;
    default: break;                        // battery and version are display-only
  }
}

// ---------- for the tablet: all items as JSON, change by key ----------
static void itemJson(int id, String& o) {
  const SetDef& d = DEFS[id];
  char b[48];
  o += "{\"k\":\""; o += d.key; o += "\",\"name\":"; jsonStr(o, LT(d.name)); o += ",\"hint\":";
  // theme isn't changeable from the tablet (a web change crashed the stick): show only, change with the wheel
  if (id == S_THEME) { jsonStr(o, L("change it on the device: Settings › Theme", "меняется на устройстве: Настройки › Тема")); o += ",\"type\":\"info\",\"v\":"; optName(id, getVal(id), b, sizeof(b)); jsonStr(o, b); o += '}'; return; }
  if (id == S_LANG)  { jsonStr(o, L("change it on the device: Settings › Language", "меняется на устройстве: Настройки › Язык")); o += ",\"type\":\"info\",\"v\":"; optLabel(id, getVal(id), b, sizeof(b)); jsonStr(o, b); o += '}'; return; }
  jsonStr(o, LT(d.hint));
  switch (d.kind) {
    case K_ENUM:
      o += ",\"type\":\"enum\",\"v\":"; optName(id, getVal(id), b, sizeof(b)); jsonStr(o, b);
      o += ",\"opts\":[";
      for (int i = 0; i < optCount(id); i++) { if (i) o += ','; optName(id, i, b, sizeof(b)); jsonStr(o, b); }
      o += "],\"labels\":[";                               // labels in the firmware language (opts are machine values)
      for (int i = 0; i < optCount(id); i++) { if (i) o += ','; optLabel(id, i, b, sizeof(b)); jsonStr(o, b); }
      o += ']';
      break;
    case K_RANGE:
      o += ",\"type\":\"range\",\"v\":"; o += getVal(id);
      o += ",\"min\":"; o += d.mn; o += ",\"max\":"; o += d.mx; o += ",\"step\":"; o += d.step; o += ",\"unit\":"; jsonStr(o, d.unit);
      break;
    case K_BOOL:
      o += ",\"type\":\"bool\",\"v\":"; o += getVal(id) ? 1 : 0;
      break;
    default:
      if (id == S_VERSION) { if (crumbWasCrash()) snprintf(b, sizeof(b), L("v%s · last reset: %s @ %s", "v%s · сброс: %s @ %s"), FW_VERSION, crumbLastReason(), crumbLastStep()); else snprintf(b, sizeof(b), "v%s", FW_VERSION); }
      else if (g_bat.ok) snprintf(b, sizeof(b), "%d%%  %.2f V", g_bat.soc, g_bat.mv / 1000.0f);
      else snprintf(b, sizeof(b), "%s", L("n/a", "нет"));
      o += ",\"type\":\"info\",\"v\":"; jsonStr(o, b);
  }
  o += '}';
}

void settingsJson(String& o) {
  o.reserve(4800);
  o = "{\"pages\":[";
  for (int p = 0; p < P_NUM; p++) {
    if (p) o += ',';
    o += "{\"id\":\""; o += PAGE_IDS[p]; o += "\",\"name\":"; jsonStr(o, LT(PAGE_NAMES[p])); o += ",\"items\":[";
    bool first = true;
    for (int i = 0; i < S_NUM; i++) if (DEFS[i].page == p) { if (!first) o += ','; first = false; itemJson(i, o); }
    o += "]}";
  }
  o += "]}";
}

bool settingsSetKey(const char* key, const char* val, String& o) {
  int id = -1;
  for (int i = 0; i < S_NUM; i++) if (!strcmp(DEFS[i].key, key)) id = i;
  const char* err = nullptr;
  if (id < 0) err = "unknown key";
  else if (DEFS[id].kind == K_INFO || id == S_THEME || id == S_LANG) err = "change it on the device";
  else {
    int v = 0;
    if (DEFS[id].kind == K_ENUM) {                        // option name (case-insensitive) or its number
      v = -1;
      char b[48];
      for (int i = 0; i < optCount(id); i++) { optName(id, i, b, sizeof(b)); if (!strcasecmp(b, val)) v = i; }
      if (v < 0 && isdigit((unsigned char)val[0])) v = atoi(val);
      if (v < 0 || v >= optCount(id)) err = "bad option";
    } else if (DEFS[id].kind == K_BOOL) {
      v = (!strcasecmp(val, "1") || !strcasecmp(val, "true") || !strcasecmp(val, "on")) ? 1 : 0;
    } else {
      if (!val[0] || !(isdigit((unsigned char)val[0]) || val[0] == '-')) err = "bad number";
      else v = atoi(val);
    }
    if (!err && !setVal(id, v)) err = "not applied";
  }
  if (err) { o = "{\"ok\":false,\"err\":"; jsonStr(o, err); o += '}'; return false; }
  o = "{\"ok\":true,\"item\":"; itemJson(id, o); o += '}';
  return true;
}

void settingsTouch() { g_setDirty = true; g_setChangedMs = millis(); }

static const char* unitLabel(const char* u) {   // units for the screen
  if (g_lang && !strcmp(u, "dBm")) return "дБм";
  if (g_lang && !strcmp(u, "s")) return "с";
  return u;
}
static const char* onOff(int v) { return v ? L("on", "вкл") : L("off", "выкл"); }
static void rangeText(char* val, size_t n, int r) {
  char nm[24]; optLabel(S_RANGE, r, nm, sizeof nm);
  if (r == 0) snprintf(val, n, "%s", nm);
  else snprintf(val, n, L("%s %d dBm", "%s %d дБм"), nm, RANGES[r].dbm);
}

void drawSettings() {
  int W = spr.width();
  spr.fillSprite(C_BG());
  int ids[S_NUM]; int n = pageItems(g_setPage, ids);
  g_setItem = constrain(g_setItem, 0, n - 1);
  int cur = ids[g_setItem];
  char batLine[64]; batSummary(batLine, sizeof(batLine));
  char rst[72] = "";
  if (cur == S_VERSION && crumbWasCrash()) snprintf(rst, sizeof(rst), L("last reset: %s @ %s", "сброс: %s @ %s"), crumbLastReason(), crumbLastStep());
  uiHeader(LT(PAGE_NAMES[g_setPage]), cur == S_BATTERY ? batLine : rst[0] ? rst : LT(DEFS[cur].hint), rst[0] != 0);
  const int rh = 18;
  int rows = min(n, SET_ROWS);
  int first = constrain(g_setItem - (rows - 1), 0, n - rows);
  for (int r = first; r < first + rows; r++) {
    int id = ids[r];
    int y = UI_TOP + 2 + (r - first) * rh, cy = y + rh / 2 - 1;
    bool sel = (r == g_setItem);
    if (sel) uiRowHighlight(y, rh - 2);
    char val[48];
    bool alert = false;
    switch (id) {
      case S_RANGE:   rangeText(val, sizeof(val), g_range); break;
      case S_WRANGE:  rangeText(val, sizeof(val), g_wRange); break;
      case S_LEDEV:   if (g_thresh == 0) snprintf(val, sizeof(val), "%s", L("off", "выкл")); else snprintf(val, sizeof(val), "+/- %d", g_thresh); break;
      case S_LEDBR:   snprintf(val, sizeof(val), "%d%%", g_ledBright); break;
      case S_SCREEN:  snprintf(val, sizeof(val), "%d%%", g_scrBright); break;
      case S_DIM:     optLabel(id, g_dimIdx, val, sizeof val); break;
      case S_VOLUME:  optLabel(id, g_sndIdx, val, sizeof val); break;
      case S_CLICKS:  strlcpy(val, onOff(g_clickEn), sizeof(val)); break;
      case S_THEME:   strlcpy(val, g_theme == THEME_KURENAI ? "kurenai" : "denko", sizeof(val)); break;
      case S_LANG:    optLabel(id, g_lang, val, sizeof val); break;
      case S_BLETICK: strlcpy(val, onOff(g_bleTicks), sizeof(val)); break;
      case S_WTICK:   strlcpy(val, onOff(g_wifiTicks), sizeof(val)); break;
      case S_BAND:    strlcpy(val, BANDS[g_band].name, sizeof(val)); break;
      case S_MIC:     strlcpy(val, onOff(g_micEn), sizeof(val)); break;
      case S_AULED:   strlcpy(val, onOff(g_auLeds), sizeof(val)); break;
      case S_AUSRC:   optLabel(id, g_auSrc, val, sizeof val); break;
      case S_CITY:    snprintf(val, sizeof(val), "%s", LT(curCity().name)); break;
      case S_VERSION: snprintf(val, sizeof(val), "v%s", FW_VERSION); break;
      case S_ALNEAR: case S_ALNEWFOR: case S_ALWARM: case S_ALSENS: case S_ALWARN: case S_ALALARM:
        snprintf(val, sizeof(val), "%d%s%s", getVal(id), DEFS[id].unit[0] && strcmp(DEFS[id].unit, "%") ? " " : "", unitLabel(DEFS[id].unit)); break;
      case S_ALNEW: case S_ALTRK: optLabel(id, getVal(id), val, sizeof(val)); break;
      case S_ALCBLE: case S_ALCRF: case S_ALCSND: case S_ALCPRB: case S_ALBLINK: case S_ALPLATE: strlcpy(val, onOff(getVal(id)), sizeof(val)); break;
      default:
        if (g_bat.ok) { snprintf(val, sizeof(val), "%d%%  %.2f V", g_bat.soc, g_bat.mv / 1000.0f); alert = g_bat.soc <= 20; }
        else          snprintf(val, sizeof(val), "%s", L("n/a", "нет"));
        break;
    }
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(sel ? C_HOT() : UI_LABEL());
    txt(LT(DEFS[id].name), UI_M + 8, cy, 2);
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(alert ? C_ALERT() : (sel ? C_ACC() : C_HOT()));
    txt(val, W - UI_M - 8, cy, 2);
    if (sel && id != S_BATTERY && id != S_VERSION) {                 // "turn the wheel" arrow
      int vw = txtW(val, 2);
      spr.setTextColor(UI_DIM());
      txt("<", W - UI_M - 16 - vw, cy, 2);
    }
  }
  uiFooter(n > 1 ? L("wheel: change  ·  click: next", "колесо: менять · нажать: далее") : L("wheel: change", "колесо: изменить"), L("side: back", "кнопка: назад"));
  present();
}

// ---------- Settings section: page submenu ----------
static void setSubtitle(char* out, size_t n) { snprintf(out, n, "%s", L("general + sections", "общие и разделы")); }
static void setEnter() { g_setPage = 0; g_setItem = 0; }
static void setLeave() { settingsSaveNow(); }
static void setInfo(int i, SubItem& it) {
  it.name = LT(PAGE_NAMES[i]); it.desc = LT(PAGE_DESCS[i]);
  int ids[S_NUM]; sprintf(it.value, "%d", pageItems(i, ids));
}
static int  setNow() { return g_setPage; }
static void setOpen(int p) { g_setPage = p; g_setItem = 0; }
static void setClose() { settingsSaveNow(); }
static void setClick() {
  int ids[S_NUM]; int n = pageItems(g_setPage, ids);
  g_setItem = n ? (g_setItem + 1) % n : 0;
}
static void setDraw(uint32_t) { drawSettings(); }

const Section settingsSection = {
  LP("Settings", "Настройки"), 200, 160, 90, iconGear, setSubtitle,
  setEnter, setLeave,
  P_NUM, setInfo, setNow, setOpen, setClose,
  setClick, settingsAdjust, nullptr, nullptr, setDraw,
};
