// ============================================================
//  pc.cpp — PC section: receives laptop lines over USB, plus the section UI
// ============================================================
#include "core/timeutil.h"
#include "core/lang.h"
#include "core/jsonutil.h"
#include "pc/pc.h"
#include "core/display.h"
#include "core/ui.h"
#include "audio/audio.h"
#include "gallery/astro.h"
#include "gallery/sat.h"
#include "link/serout.h"
#include "web/web.h"

PcStat g_pc;
int    g_pcPage = 0;
int      g_pcLastCmd = 0;
uint32_t g_pcLastCmdMs = 0;

static char   s_line[1024];
static size_t s_len = 0;
static bool   s_discard = false;

static void parseList(const char* v, uint8_t* out, int& n) {
  n = 0;
  while (*v && n < PC_CORES) {
    out[n++] = (uint8_t)constrain(atoi(v), 0, 100);
    const char* c = strchr(v, ',');
    if (!c) break;
    v = c + 1;
  }
}

static void parseVol(char* s) {                      // "PV vol=35 mute=0" — laptop volume
  char* save = nullptr;
  for (char* tok = strtok_r(s + 3, " ", &save); tok; tok = strtok_r(nullptr, " ", &save)) {
    if (!strncmp(tok, "vol=", 4))  g_pc.vol = constrain(atoi(tok + 4), 0, 100);
    if (!strncmp(tok, "mute=", 5)) g_pc.mute = atoi(tok + 5) != 0;
  }
  g_pc.volT = millis();
}

// "PM\t<playing 0/1>\t<pos>\t<dur>\t<app>\t<title>\t<artist>" (TAB-separated fields:
// titles contain spaces). A bare "PM" means nothing is playing.
static void copyField(char* dst, size_t cap, const char* src) { strncpy(dst, src ? src : "", cap - 1); dst[cap - 1] = 0; }
static void parseMedia(char* s) {
  PcStat& p = g_pc;
  char* f[7] = {};
  int n = 0;
  char* save = nullptr;
  for (char* tok = strtok_r(s, "\t", &save); tok && n < 7; tok = strtok_r(nullptr, "\t", &save)) f[n++] = tok;
  p.trkT = millis();
  if (n < 6) { p.trk = false; p.title[0] = p.artist[0] = p.app[0] = 0; return; }
  p.trk = true;
  p.playing = atoi(f[1]) != 0;
  p.pos = atoi(f[2]); p.dur = atoi(f[3]);
  copyField(p.app, sizeof(p.app), f[4]);
  copyField(p.title, sizeof(p.title), f[5]);
  copyField(p.artist, sizeof(p.artist), n > 6 ? f[6] : "");
}

// "PA <volume*10> <freq> <32 bands hex> <64 samples hex> <64 L,R pairs hex> <stereo 0/1>"
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }
static bool unhex(const char* h, uint8_t* out, int n) {
  if ((int)strlen(h) < n * 2) return false;
  for (int i = 0; i < n; i++) out[i] = (uint8_t)(hexv(h[2 * i]) << 4 | hexv(h[2 * i + 1]));
  return true;
}
static void parseAudio(char* s) {
  char* f[7] = {};
  int n = 0; char* save = nullptr;
  for (char* tok = strtok_r(s + 3, " ", &save); tok && n < 7; tok = strtok_r(nullptr, " ", &save)) f[n++] = tok;
  if (n < 6) return;
  static uint8_t bands[32], wave[64], xy[128];
  if (!unhex(f[2], bands, 32) || !unhex(f[3], wave, 64) || !unhex(f[4], xy, 128)) return;
  audioPcFrame(atoi(f[0]) / 10.0f, atoi(f[1]), bands, (const int8_t*)wave, (const int8_t*)xy, atoi(f[5]) != 0);
}

static void parseLine(char* s) {
  if (s[0] == '@') { webLinkLine(s); return; }      // PC app (web.cpp, "@…")
  if (!strncmp(s, "PA ", 3)) { parseAudio(s); return; }
  if (!strncmp(s, "TLE\t", 4)) { satFromLine(s); return; }   // satellite orbits from the laptop
  if (!strncmp(s, "PP\t", 3)) { pcCityLine(s); return; }      // programs for the process city
  if (!strncmp(s, "PV ", 3)) { parseVol(s); return; }
  if (!strncmp(s, "PM", 2) && (s[2] == '\t' || s[2] == 0)) { parseMedia(s); return; }
  if (strncmp(s, "PC ", 3) != 0) return;            // not ours - skip
  PcStat& p = g_pc;
  uint32_t ts = 0; long tz = 0;
  char* save = nullptr;
  for (char* tok = strtok_r(s + 3, " ", &save); tok; tok = strtok_r(nullptr, " ", &save)) {
    char* eq = strchr(tok, '=');
    if (!eq) continue;
    *eq = 0;
    const char* k = tok; const char* v = eq + 1;
    if      (!strcmp(k, "c"))    parseList(v, p.core, p.nCores);
    else if (!strcmp(k, "top"))  strlcpy(p.top, v, sizeof(p.top));
    else if (!strcmp(k, "host")) strlcpy(p.host, v, sizeof(p.host));
    else if (!strcmp(k, "ts"))   ts = strtoul(v, nullptr, 10);
    else if (!strcmp(k, "tz"))   tz = atol(v);
    else if (!strcmp(k, "bat"))  p.bat = atoi(v);
    else if (!strcmp(k, "plug")) p.plug = atoi(v) > 0;
    else if (!strcmp(k, "up"))   p.up = strtoul(v, nullptr, 10);
    else {
      float f = strtof(v, nullptr);
      if      (!strcmp(k, "cpu"))  p.cpu = f;
      else if (!strcmp(k, "freq")) p.freq = f;
      else if (!strcmp(k, "ram"))  p.ram = f;
      else if (!strcmp(k, "ramu")) p.ramU = f;
      else if (!strcmp(k, "ramt")) p.ramT = f;
      else if (!strcmp(k, "dsk"))  p.disk = f;
      else if (!strcmp(k, "dr"))   p.dR = f;
      else if (!strcmp(k, "dw"))   p.dW = f;
      else if (!strcmp(k, "nd"))   p.nD = f;
      else if (!strcmp(k, "nu"))   p.nU = f;
    }
  }
  if (ts > 1700000000) astroSetTime(ts, (int32_t)tz);    // exact time, for sky and satellites
  p.have = true;
  p.t = millis();
  if (p.histN < PC_HIST) { p.cpuH[p.histN] = p.cpu; p.ramH[p.histN] = p.ram; p.histN++; }
  else {
    memmove(p.cpuH, p.cpuH + 1, sizeof(float) * (PC_HIST - 1)); p.cpuH[PC_HIST - 1] = p.cpu;
    memmove(p.ramH, p.ramH + 1, sizeof(float) * (PC_HIST - 1)); p.ramH[PC_HIST - 1] = p.ram;
  }
}

void pcPoll(uint32_t) {
  int budget = 2048;                                 // don't get stuck if there is a lot of data (laptop audio ~15 KB/s)
  while (Serial.available() && budget--) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (s_discard) { s_discard = false; s_len = 0; continue; }
      if (s_len) { s_line[s_len] = 0; parseLine(s_line); s_len = 0; }
    } else if (!s_discard) {
      if (s_len < sizeof(s_line) - 1) s_line[s_len++] = c;
      else { s_len = 0; s_discard = true; }          // line too long: discard until EOL
    }
  }
}

// "no link" only after 10 s of silence (Windows sometimes delays sending)
bool pcLive(uint32_t now) { return g_pc.have && msSince(now, g_pc.t) < 10000; }

// ---------- remote: commands to the laptop ----------
// The laptop program reads "KEY ..." lines and presses media keys.
static void pcSend(const char* cmd, int code) {
  g_pcLastCmd = code; g_pcLastCmdMs = millis();
  if (!pcLive(millis())) return;                     // program not running - nobody to send to
  serOutLine(cmd);                                   // shared USB queue: the line goes out whole, no waiting
}

// tablet remote (/api/pckey): allowed keys only
bool pcKey(const char* name) {
  static const struct { const char* n; int code; } K[] = { { "VOLUP", 1 }, { "VOLDN", -1 }, { "PLAY", 2 }, { "MUTE", 3 }, { "NEXT", 4 }, { "PREV", 5 } };
  for (auto& k : K) if (!strcmp(name, k.n)) {
    char cmd[16]; snprintf(cmd, sizeof(cmd), "KEY %s", k.n);
    pcSend(cmd, k.code);
    return pcLive(millis());
  }
  return false;
}

void pcSendLine(const char* s) {
  if (!pcLive(millis())) return;                     // program not running - nobody to send to
  serOutLine(s);
}

// ---------- section ----------
static void pcSubtitle(char* out, size_t n) {
  if (pcLive(millis())) snprintf(out, n, L("%d%% cpu", "%d%% ЦП"), (int)g_pc.cpu);
  else                  snprintf(out, n, "%s", L("via USB", "по USB"));
}
static void pcEnter() { g_pcPage = 0; }
static void pcLeave() {}
static void pcClick() {
  if (g_pcPage == 3) pcSend("KEY PLAY", 2);          // on the music page: pause/play
  else if (g_pcPage == 2) pcCityNext();             // city: spotlight to the next tower
  // overview and cores: click does nothing (the wheel flips pages)
}
static void pcRotate(int dir) {
  if (g_pcPage == 3) { pcSend(dir > 0 ? "KEY VOLUP" : "KEY VOLDN", dir > 0 ? 1 : -1); return; }   // wheel = volume
  static uint32_t last = 0;
  if (millis() - last < 150) return;
  last = millis();
  g_pcPage = (g_pcPage + dir + PC_PAGES) % PC_PAGES;
}
static bool pcBack() {                               // side button on the music page goes to overview, not out of the section
  if (g_pcPage == 3) { g_pcPage = 0; return true; }
  return false;
}
static void pcDraw(uint32_t now) {
  g_uiPages = pcLive(now) ? PC_PAGES : 0; g_uiPage = g_pcPage;   // page dots (the section has no submenu)
  if (!pcLive(now)) drawPcWaiting(now);
  else if (g_pcPage == 0) drawPc(now);
  else if (g_pcPage == 1) drawPcCores(now);
  else if (g_pcPage == 2) drawPcCity(now);
  else drawPcMedia(now);
}

const Section pcSection = {
  LP("PC", "ПК"), 120, 220, 200, iconLaptop, pcSubtitle,
  pcEnter, pcLeave,
  0, nullptr, nullptr, nullptr, nullptr,
  pcClick, pcRotate, pcBack, nullptr, pcDraw,
};

// ---------- for the tablet (SSE stream, once a second) ----------
// {"live":1,"host":..,"cpu":..,"c":[..],"ram":..,"ru":..,"rt":..,"disk":..,"dr":..,"dw":..,"nd":..,"nu":..,
//  "bat":..,"plug":..,"up":..,"top":..,"trk":{...},"city":[...]}
void pcStreamJson(String& o) {
  uint32_t now = millis();
  const PcStat& p = g_pc;
  o.reserve(900);
  o = "{\"live\":"; o += pcLive(now) ? 1 : 0;
  if (!p.have) { o += '}'; return; }
  o += ",\"age\":"; o += msSince(now, p.t) / 1000;
  o += ",\"host\":"; jsonStr(o, p.host);
  o += ",\"cpu\":"; o += String(p.cpu, 1); o += ",\"ghz\":"; o += String(p.freq, 2);
  o += ",\"c\":[";
  for (int i = 0; i < p.nCores && i < PC_CORES; i++) { if (i) o += ','; o += p.core[i]; }
  o += "],\"ram\":"; o += String(p.ram, 1); o += ",\"ru\":"; o += String(p.ramU, 1); o += ",\"rt\":"; o += String(p.ramT, 1);
  o += ",\"disk\":"; o += String(p.disk, 0); o += ",\"dr\":"; o += String(p.dR, 1); o += ",\"dw\":"; o += String(p.dW, 1);
  o += ",\"nd\":"; o += String(p.nD, 0); o += ",\"nu\":"; o += String(p.nU, 0);
  o += ",\"bat\":"; o += p.bat; o += ",\"plug\":"; o += p.plug ? 1 : 0; o += ",\"up\":"; o += p.up;
  o += ",\"top\":"; jsonStr(o, p.top);
  o += ",\"vol\":"; o += p.vol; o += ",\"mute\":"; o += p.mute ? 1 : 0;
  if (p.trk) {
    int pos = p.pos;
    if (pos >= 0 && p.playing) pos += msSince(now, p.trkT) / 1000;   // extrapolate like the Media screen
    if (p.dur > 0 && pos > p.dur) pos = p.dur;
    o += ",\"trk\":{\"t\":"; jsonStr(o, p.title); o += ",\"a\":"; jsonStr(o, p.artist);
    o += ",\"app\":"; jsonStr(o, p.app); o += ",\"play\":"; o += p.playing ? 1 : 0;
    o += ",\"pos\":"; o += pos; o += ",\"dur\":"; o += p.dur; o += '}';
  }
  if (pcCityLive(now)) { o += ",\"city\":"; pcCityJson(o); }
  o += '}';
}
