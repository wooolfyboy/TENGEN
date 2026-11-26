// ============================================================
//  web.cpp — Web section: own Wi-Fi AP (SSID/password derived from the MAC), web server on
//  192.168.4.1 and a catch-all DNS so phones offer to open the page. The page polls /api/ble,
//  /api/wifi and /api/info. Active only while the section is open.
// ============================================================
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <qrcode.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include "core/jsonutil.h"
#include "core/timeutil.h"
#include "web/web.h"
#include "core/perf.h"
#include "core/board.h"
#include "web/web_page.h"
#include "core/lang.h"
#include "web/tablet_page.h"       // tablet app (gzip, built by tools/webapp/build.py): both languages, picks the device language (st.lang)
#include <lwip/sockets.h>
#include "ble/ble.h"
#include "wifi/wifi.h"
#include "wifi/wifi_model.h"
#include "subghz/subghz.h"
#include "subghz/subghz_model.h"
#include "subghz/sensors.h"
#include "audio/audio.h"
#include "pc/pc.h"
#include "gallery/astro.h"
#include "gallery/sat.h"
#include "gallery/weather.h"
#include "core/settings.h"
#include "core/version.h"
#include "core/crumb.h"
#include "core/battery.h"
#include "core/display.h"
#include "core/menu.h"
#include "core/power.h"
#include "core/leds.h"
#include "probe/ir.h"
#include "probe/nfc.h"
#include "locator/locator.h"
#include "link/serout.h"
#include <mbedtls/base64.h>

static WebServer* s_srv = nullptr;
static DNSServer  s_dns;
static bool       s_on = false, s_ok = false;
static char       s_ssid[24], s_pass[12];
static uint32_t   s_reqs = 0, s_scanMs = 0;
static bool       s_scanned = false;
static String     s_wifiJson = "{\"scanned\":false,\"nets\":[]}";
static uint32_t   s_wifiGen = 0;              // bumped on every new scan result (tablet stream)
static uint8_t    s_qr[45 * 45];
static int        s_qrN = 0;
static int        s_netCount = 0;
static uint32_t   s_subMs = 0, s_sndMs = 0;     // last time the Sub-GHz / Sound tabs were polled
static bool       s_subOn = false, s_sndOn = false;
static uint32_t   s_irMs = 0, s_nfcMs = 0, s_locMs = 0;      // IR, NFC and Locator 2x for the tablet (while subscribed)
static bool       s_irOn = false, s_nfcOn = false, s_locOn = false;
static const char* s_tab = "";
static int8_t      s_bleWanted = -1;            // last webTick decision for Bluetooth (-1: recompute)

// ---------- Wi-Fi firmware update (OTA) ----------
// Accepts a plain firmware.bin or a merged image for 0x0 (app at offset 0x10000, the head is skipped).
// The new firmware goes to the second slot; the bootloader rolls back unless it runs 15 s (main.cpp).
static int      s_otaPct = -1;                  // -1: no upload
static String   s_otaErr;
static uint8_t* s_otaHead = nullptr;            // first 0x8002 bytes: decide whether the file is merged
static size_t   s_otaHeadN = 0, s_otaPos = 0, s_otaSkip = 0;
static bool     s_otaStarted = false;
static const size_t OTA_PROBE = 0x8002, OTA_APP_OFS = 0x10000;

static void qrStore(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q);
  if (n > 45) { s_qrN = 0; return; }
  s_qrN = n;
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) s_qr[y * n + x] = esp_qrcode_get_module(q, x, y) ? 1 : 0;
}

static void otaFreeHead() {
  if (s_otaHead) { free(s_otaHead); s_otaHead = nullptr; }
}

static void makeNames() {
  uint8_t m[6]; WiFi.macAddress(m);
  snprintf(s_ssid, sizeof(s_ssid), "TENGEN-%02X%02X", m[4], m[5]);
  // Random 8-digit WPA2 password generated once per device and stored in NVS
  // (not derivable from the public MAC broadcast in Wi-Fi beacons).
  Preferences p;
  p.begin("web", false);
  String saved = p.getString("pass", "");
  if (saved.length() == 8) {
    strlcpy(s_pass, saved.c_str(), sizeof(s_pass));
  } else {
    snprintf(s_pass, sizeof(s_pass), "%08lu", (unsigned long)(esp_random() % 100000000UL));
    p.putString("pass", s_pass);
  }
  p.end();
  char wifiQr[80];
  snprintf(wifiQr, sizeof(wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", s_ssid, s_pass);
  esp_qrcode_config_t cfg = { qrStore, 6, ESP_QRCODE_ECC_LOW };
  esp_qrcode_generate(&cfg, wifiQr);
}

static const char* encStr(wifi_auth_mode_t e) {
  switch (e) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: case WIFI_AUTH_WPA_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA3_PSK: case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    default: return "?";
  }
}

// Network scan one channel at a time: scanning all 13 channels at once (~1.2 s) pulled the
// radio off the AP channel and clients dropped. Now every SCAN_STEP_MS we listen to one channel
// for ~90 ms; a full round takes ~20 s; results are kept and expire after NET_FORGET_MS.
#define NET_MAX 64
static const uint32_t SCAN_STEP_MS = 1500, NET_FORGET_MS = 60000;
struct NetRec { uint8_t b[6]; char ssid[33]; int8_t r; uint8_t ch, enc; uint32_t t; };
static NetRec   s_nets[NET_MAX];
static int      s_nNets = 0;
static uint8_t  s_scanCh = 1;

static void netsJson() {
  uint32_t now = millis();
  int w = 0;                                       // drop long-unheard networks
  for (int i = 0; i < s_nNets; i++) if (now - s_nets[i].t < NET_FORGET_MS) s_nets[w++] = s_nets[i];
  s_nNets = w;
  String o; o.reserve(64 + s_nNets * 90);
  o += "{\"scanned\":true,\"nets\":[";
  for (int i = 0; i < s_nNets; i++) {
    const NetRec& n = s_nets[i];
    if (i) o += ',';
    char mac[18]; snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", n.b[0], n.b[1], n.b[2], n.b[3], n.b[4], n.b[5]);
    const char* v = ouiName(n.b);                  // vendor from the address prefix
    o += "{\"s\":"; jsonStr(o, n.ssid);
    o += ",\"b\":\""; o += mac; o += '"';
    o += ",\"v\":"; jsonStr(o, v ? v : "");
    o += ",\"r\":"; o += n.r;
    o += ",\"c\":"; o += n.ch;
    o += ",\"e\":\""; o += encStr((wifi_auth_mode_t)n.enc); o += "\"}";
  }
  o += "]}";
  s_wifiJson = o;
  s_netCount = s_nNets;
  s_wifiGen++;
  s_scanned = true;
}

static void scanCollect() {
  int n = WiFi.scanComplete();
  if (n < 0) return;                              // still running (or never started)
  uint32_t now = millis();
  for (int i = 0; i < n; i++) {
    uint8_t* b = WiFi.BSSID(i);
    int k = 0;
    while (k < s_nNets && memcmp(s_nets[k].b, b, 6)) k++;
    if (k == s_nNets) { if (s_nNets >= NET_MAX) continue; s_nNets++; memcpy(s_nets[k].b, b, 6); }
    NetRec& r = s_nets[k];
    strlcpy(r.ssid, WiFi.SSID(i).c_str(), sizeof(r.ssid));
    r.r = WiFi.RSSI(i); r.ch = WiFi.channel(i); r.enc = (uint8_t)WiFi.encryptionType(i); r.t = now;
  }
  WiFi.scanDelete();
  netsJson();
}

// which tab is open on the phone: the page sends it in every request (?tab=)
static void noteTab() {
  if (!s_srv->hasArg("tab")) return;
  static const char* const T[][2] = { { "ov", "overview" }, { "ble", "bluetooth" }, { "wifi", "wi-fi" },
                                      { "sub", "sub-ghz" }, { "snd", "sound" }, { "sys", "system" },
                                      { "upd", "update" } };
  String t = s_srv->arg("tab");
  for (auto& p : T) if (t == p[0]) { s_tab = p[1]; return; }
}
static bool tabIs(const char* a) { return strcmp(s_tab, a) == 0; }

static void sendJson(const String& s) {
  noteTab();
  s_reqs++;
  // otherwise header and data go as two segments and the phone waits ~0.2-0.5 s
  // for the second (Nagle + delayed ACK)
  s_srv->client().setNoDelay(true);
  s_srv->sendHeader("Cache-Control", "no-store");
  s_srv->sendHeader("Access-Control-Allow-Origin", "*");   // the tablet app lives on its own origin (localhost)
  s_srv->send(200, "application/json", s);
}

static void otaDrawProgress() {
  WebView v = { s_ssid, s_pass, "192.168.4.1", 1, s_reqs, true, "update" };
  drawWebOta(v, s_otaPct, s_otaErr.c_str());
}

static bool otaWrite(uint8_t* d, size_t n) {
  // skip the bootloader and partition table of a merged file
  if (s_otaSkip) {
    size_t k = min(n, s_otaSkip);
    d += k; n -= k; s_otaSkip -= k;
    if (!n) return true;
  }
  if (!s_otaStarted) {
    if (d[0] != 0xE9) { s_otaErr = "not an ESP32 firmware file"; return false; }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { s_otaErr = Update.errorString(); return false; }
    s_otaStarted = true;
  }
  crumbFeed();                                     // flash writing is slow; tell the watchdog we are alive
  if (Update.write(d, n) != n) { s_otaErr = Update.errorString(); return false; }
  return true;
}

static void otaUpload() {
  HTTPUpload& up = s_srv->upload();
  if (up.status == UPLOAD_FILE_START) {
    s_otaErr = ""; s_otaPct = 0; s_otaPos = 0; s_otaHeadN = 0; s_otaSkip = 0; s_otaStarted = false;
    bleRadioOff();                                 // all airtime goes to the upload
    if (!s_otaHead) s_otaHead = (uint8_t*)malloc(OTA_PROBE);
    if (!s_otaHead) s_otaErr = "no memory";
    otaDrawProgress();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_otaErr.length()) return;
    uint8_t* d = up.buf; size_t n = up.currentSize;
    s_otaPos += n;
    if (s_otaHeadN < OTA_PROBE && s_otaHead) {     // collect the file head
      size_t k = min(n, OTA_PROBE - s_otaHeadN);
      memcpy(s_otaHead + s_otaHeadN, d, k);
      s_otaHeadN += k; d += k; n -= k;
      if (s_otaHeadN < OTA_PROBE) return;
      bool merged = s_otaHead[0x8000] == 0xAA && s_otaHead[0x8001] == 0x50;   // partition table at 0x8000
      bool ok = true;
      if (merged) s_otaSkip = OTA_APP_OFS - OTA_PROBE;                        // skip the rest up to 0x10000
      else ok = otaWrite(s_otaHead, OTA_PROBE);
      otaFreeHead();                                                          // release 32 KB probe buffer immediately
      if (!ok) return;
    }
    if (n && !otaWrite(d, n)) return;
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw > 400) {               // device screen: progress
      lastDraw = millis();
      long cl = s_srv->header("Content-Length").toInt();
      size_t est = cl > 100000 ? (size_t)cl : 2100000;
      s_otaPct = min(99, (int)((uint64_t)s_otaPos * 100 / est));
      otaDrawProgress();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    otaFreeHead();
    if (!s_otaErr.length() && s_otaHeadN < OTA_PROBE)                // file smaller than 32 KB
      s_otaErr = "file is too small";
    if (!s_otaErr.length() && !Update.end(true)) s_otaErr = Update.errorString();
    if (s_otaErr.length() && s_otaStarted) Update.abort();
    s_otaPct = s_otaErr.length() ? -2 : 100;
    otaDrawProgress();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    otaFreeHead();
    if (s_otaStarted) Update.abort();
    s_otaErr = "upload interrupted"; s_otaPct = -2;
    otaDrawProgress();
  }
}

static void otaDone() {
  otaFreeHead();
  s_reqs++;
  bool ok = s_otaPct == 100 && !s_otaErr.length();
  String o = "{\"ok\":"; o += ok ? "true" : "false";
  o += ",\"err\":"; jsonStr(o, s_otaErr.c_str()); o += '}';
  s_srv->client().setNoDelay(true);
  s_srv->send(200, "application/json", o);
  if (ok) {                                        // reply to the phone and reboot into the new firmware
    delay(600);
    ESP.restart();
  }
  s_otaPct = -1;
  s_bleWanted = -1;                                // webTick restores the radio according to what is being viewed
}

// ============================================================
//  Tablet side channel: an SSE stream on port 81. The tablet opens
//  http://192.168.4.1:81/stream?want=ble,wifi,snd and keeps it open; the device pushes events:
//    st (2 s: battery, uptime, clients)   ble (1 s)   wifi (after a scan)
//    snd (~25/s: bands, level, wave; only if requested)   sub (~7/s: CC1101 spectrum; &sm= mode, &sb= band)
//    pc (1 s: laptop load, track, process city)   sens (each new 433 MHz packet and 1 s; want=sens)
//    sat (on connect: orbits and city, the tablet computes the sky)   ir (each IR burst)   nfc (each card tap)
//  Sends never block: if the tablet cannot keep up, sound frames are dropped.
// ============================================================
#define SSE_NET 2                                // Wi-Fi tablets
#define SSE_MAX 3                                // + the PC app over USB (last slot)
#define USB_SLOT 2
static uint32_t s_sseBytes = 0;                  // total sent to tablets (link screen)
struct SseCli {
  NetworkClient c;
  bool on = false, ble = false, wifi = false, snd = false, sub = false, pc = false, wx = false, ir = false, nfc = false, loc = false, satSent = false;
  int8_t sm = 0, sb = 1;                         // Sub-GHz mode and band
  bool sens = false;                             // 433 MHz sensors (sensors.h)
  uint32_t tSens = 0, sensSeen = 0;
  String pend;                                   // unsent tail (flushed without blocking)
  bool usb = false;                              // this "tablet" is the PC app over USB (link)
  uint32_t seenMs = 0;                           // USB: last sign of life from the PC
  String wantQ;                                  // USB: subscriptions (want=... string)
  uint32_t tSt = 0, tBle = 0, tSnd = 0, tSub = 0, tPc = 0, tWx = 0, tLoc = 0, wifiSeen = 0, irSeen = 0, nfcSeen = 0;
};
static NetworkServer* s_sseSrv = nullptr;
static SseCli s_sse[SSE_MAX];

static bool sseFlush(SseCli& k) {                // true: everything was sent
  if (k.usb) return true;                        // USB writes go through the shared queue (link/serout)
  while (k.pend.length()) {
    int n = send(k.c.fd(), k.pend.c_str(), k.pend.length(), MSG_DONTWAIT);
    if (n > 0) { k.pend.remove(0, n); s_sseBytes += n; continue; }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;   // buffer full, retry later
    k.on = false; k.c.stop(); return false;      // connection died
  }
  return true;
}
static void sseEvent(SseCli& k, const char* ev, const String& data, bool droppable) {
  if (!k.on) return;
  if (k.usb) {                                   // PC over USB: "@E event json", same JSON as the tablet gets
    size_t el = strlen(ev);
    if (!serOutBegin(4 + el + data.length(), droppable)) return;   // PC is not keeping up: skip the frame
    serOutPart("@E ", 3); serOutPart(ev, el); serOutPart(" ", 1); serOutPart(data.c_str(), data.length()); serOutEnd();
    s_sseBytes += 5 + el + data.length();
    return;
  }
  if (k.pend.length() && droppable) { sseFlush(k); return; }   // not keeping up: skip the sound frame
  if (k.pend.length() > 24000) { k.on = false; k.c.stop(); return; }
  k.pend += "event: "; k.pend += ev; k.pend += "\ndata: "; k.pend += data; k.pend += "\n\n";
  sseFlush(k);
}

// client subscriptions from the request line "...want=ble,wifi&sm=0&sb=1 ..." (same for Wi-Fi and USB)
static void parseWant(SseCli& k, const String& req) {
  int q = req.indexOf("want="); String want = q >= 0 ? req.substring(q + 5, req.indexOf(' ', q)) : String("ble,wifi");
  k.ble = want.indexOf("ble") >= 0; k.wifi = want.indexOf("wifi") >= 0; k.snd = want.indexOf("snd") >= 0;
  k.sub = want.indexOf("sub") >= 0; k.pc = want.indexOf("pc") >= 0; k.wx = want.indexOf("wx") >= 0;
  k.ir = want.indexOf("ir") >= 0; k.nfc = want.indexOf("nfc") >= 0; k.loc = want.indexOf("loc") >= 0;
  k.irSeen = g_ir.gen; k.nfcSeen = g_nfc.gen;     // do not replay old taps and cards, only new ones
  auto qInt = [&](const char* key, int def) { int p = req.indexOf(key); return p >= 0 ? req.substring(p + strlen(key)).toInt() : def; };
  k.sens = want.indexOf("sens") >= 0;
  k.sm = (int8_t)qInt("sm=", 0); k.sb = (int8_t)qInt("sb=", 1);
}

static void sseAccept() {
  NetworkClient nc = s_sseSrv->accept();
  if (!nc) return;
  // Bounded header read (max 768 bytes, max 80 ms) so a slow or malicious client
  // cannot stall loop() or exhaust the heap via readStringUntil().
  char hdr[768];
  size_t hn = 0;
  uint32_t t0 = millis();
  bool done = false;
  while (millis() - t0 < 80 && nc.connected() && hn < sizeof(hdr) - 1) {
    int av = nc.available();
    if (av <= 0) { if (hn > 0 && millis() - t0 > 20) delayMicroseconds(250); else break; continue; }
    int rd = nc.read((uint8_t*)(hdr + hn), min((size_t)av, sizeof(hdr) - 1 - hn));
    if (rd <= 0) break;
    hn += (size_t)rd;
    hdr[hn] = 0;
    if (strstr(hdr, "\r\n\r\n") || strstr(hdr, "\n\n")) { done = true; break; }
  }
  char* eol = strpbrk(hdr, "\r\n");
  if (eol) *eol = 0;
  if (!done || strncmp(hdr, "GET /stream", 11) != 0) {
    nc.print("HTTP/1.1 404 Not Found\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\n\r\n");
    nc.stop(); return;
  }
  String req(hdr); req += ' ';
  int slot = 0;
  for (int i = 0; i < SSE_NET; i++) if (!s_sse[i].on) { slot = i; break; }   // both busy: evict the first
  SseCli& k = s_sse[slot];
  if (k.on) k.c.stop();
  k = SseCli();
  k.c = nc; k.c.setNoDelay(true); k.on = true;
  k.pend.reserve(4096);                          // unsent tail: one buffer for the whole session, no reallocation
  parseWant(k, req);
  k.pend = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
           "Connection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\nretry: 1500\n\n";
  sseFlush(k);
  k.tSt = k.tBle = 0; k.wifiSeen = 0;
  s_reqs++;
}

// what the tablets need now (to switch off unneeded radios)
static int sseClients(bool& ble, bool& wifi, bool& snd, int& subM, int& subB, bool& wx) {
  int n = 0; ble = wifi = snd = wx = false; subM = -1; subB = 1;
  for (auto& k : s_sse) if (k.on) { n++; ble |= (k.ble || k.loc); wifi |= k.wifi; snd |= k.snd; wx |= k.wx; if (k.sub) { subM = k.sm; subB = k.sb; } }
  return n;
}

static void sseTick(uint32_t now) {
  static String o;                                 // reused scratch buffer: avoids ~30 heap allocs/sec
  o.reserve(2048);
  if (s_sseSrv) sseAccept();
  for (auto& k : s_sse) {
    if (!k.on) continue;
    if (k.usb) { if (msSince(now, k.seenMs) > 6000) { k = SseCli(); continue; } }   // app closed / cable pulled
    else if (!k.c.connected()) { k.on = false; k.c.stop(); continue; }
    if (k.pend.length()) sseFlush(k);
    if (!k.tSt || now - k.tSt >= 2000) {
      k.tSt = now;
      o = "{\"name\":\"" DEVICE_NAME "\",\"up\":"; o += millis() / 1000;
      o += ",\"bat\":"; o += g_bat.ok ? g_bat.soc : -1; o += ",\"mv\":"; o += g_bat.mv;
      o += ",\"usb\":"; o += g_bat.bus ? 1 : 0; o += ",\"chg\":"; o += g_bat.chg;
      o += ",\"cli\":"; o += WiFi.softAPgetStationNum(); o += ",\"heap\":"; o += ESP.getFreeHeap() / 1024;
      o += ",\"theme\":\""; o += g_theme == THEME_KURENAI ? "kurenai" : "denko"; o += '"';
      o += ",\"ver\":\"" FW_VERSION "\",\"lang\":\""; o += LANG_CODE; o += '"';
      o += ",\"rst\":"; jsonStr(o, crumbLastReason()); o += ",\"rstStep\":"; jsonStr(o, crumbLastStep());
      o += ",\"radio\":\""; o += bleRadioActive() ? "b" : ""; o += s_scanMs && now - s_scanMs < 25000 ? "w" : "";
      o += s_subOn ? "s" : ""; o += s_sndOn ? "m" : ""; o += pcLive(now) ? "p" : ""; o += s_irOn ? "i" : ""; o += s_nfcOn ? "n" : ""; o += '"';
      { // device speed: avg/worst loop pass and screen frame (ms), frames/s, share of screen lines sent (%)
        char lp[64]; snprintf(lp, sizeof lp, ",\"lp\":[%.1f,%.1f,%.1f,%.1f,%u,%u]", g_perf.loopAvgUs / 1000.0f, g_perf.loopMaxUs / 1000.0f,
                              g_perf.drawAvgUs / 1000.0f, g_perf.drawMaxUs / 1000.0f, g_perf.fps, g_perf.pushPct);
        o += lp;
      }
      o += '}';
      sseEvent(k, "st", o, false);
      if (!k.satSent) {                          // once: city and orbits, the tablet computes sky and globe
        k.satSent = true;
        const City& c = curCity();
        o = "{\"city\":"; jsonStr(o, LT(c.name));
        o += ",\"lat\":"; o += String(c.lat, 3); o += ",\"lon\":"; o += String(c.lon, 3);
        o += ",\"sats\":"; satJson(o); o += '}';
        sseEvent(k, "sat", o, false);
      }
    }
    if (k.ble && (!k.tBle || now - k.tBle >= 1000)) { k.tBle = now; o = ""; bleStreamJson(o); sseEvent(k, "ble", o, false); }
    if (k.wifi && k.wifiSeen != s_wifiGen) { k.wifiSeen = s_wifiGen; sseEvent(k, "wifi", s_wifiJson, false); }
    if (k.snd && now - k.tSnd >= 40) { k.tSnd = now; o = ""; audioStreamJson(o); sseEvent(k, "snd", o, true); }
    if (k.sub && s_subOn && now - k.tSub >= 140) { k.tSub = now; o = ""; subJson(o); sseEvent(k, "sub", o, true); }
    if (k.pc && now - k.tPc >= 1000) { k.tPc = now; o = ""; pcStreamJson(o); sseEvent(k, "pc", o, false); }
    if (k.wx && now - k.tWx >= 1000) { k.tWx = now; o = ""; wxJson(o); sseEvent(k, "wx", o, false); }
    if (k.sens && now - k.tSens >= 150 && (k.sensSeen != g_sensGen || now - k.tSens >= 1000)) {
      k.tSens = now; k.sensSeen = g_sensGen; String o; o.reserve(1536); sensJson(o, now); sseEvent(k, "sens", o, false);
    }
    if (k.ir && k.irSeen != g_ir.gen) { k.irSeen = g_ir.gen; String o; o.reserve(512); irJson(o); sseEvent(k, "ir", o, false); }
    if (k.nfc && k.nfcSeen != g_nfc.gen) { k.nfcSeen = g_nfc.gen; String o; o.reserve(384); nfcJson(o); sseEvent(k, "nfc", o, false); }
    if (k.loc && now - k.tLoc >= 250) { k.tLoc = now; o = ""; locJson(o); sseEvent(k, "loc", o, true); }
  }
}

// ============================================================
//  /api/* — shared handlers for the tablet over Wi-Fi (HTTP) and the PC app over USB
//  ("@Q id /api/...?...", see webLinkLine)
// ============================================================
struct ApiArgs {
  virtual String get(const char* k) const = 0;
  virtual bool has(const char* k) const = 0;
  virtual ~ApiArgs() {}
};
struct HttpArgs : ApiArgs {
  String get(const char* k) const override { return s_srv ? s_srv->arg(k) : String(); }
  bool has(const char* k) const override { return s_srv && s_srv->hasArg(k); }
};
// arguments from "a=1&b=%D0%9F..." (USB): %XX and "+" are decoded
struct QueryArgs : ApiArgs {
  String q;
  explicit QueryArgs(const String& s) : q(s) {}
  int find(const char* k) const {
    size_t kl = strlen(k); int p = 0;
    while (p <= (int)q.length()) {
      int e = q.indexOf('&', p); if (e < 0) e = q.length();
      if ((size_t)(e - p) >= kl && !strncmp(q.c_str() + p, k, kl) && (e - p == (int)kl || q[p + kl] == '=')) return p + kl;
      p = e + 1;
    }
    return -1;
  }
  bool has(const char* k) const override { return find(k) >= 0; }
  String get(const char* k) const override {
    int p = find(k); if (p < 0) return String();
    if (p < (int)q.length() && q[p] == '=') p++;
    int e = q.indexOf('&', p); if (e < 0) e = q.length();
    String out; out.reserve(e - p);
    for (int i = p; i < e; i++) {
      char c = q[i];
      if (c == '+') out += ' ';
      else if (c == '%' && i + 2 < e) {
        char h[3] = { q[i + 1], q[i + 2], 0 }; out += (char)strtol(h, nullptr, 16); i += 2;
      } else out += c;
    }
    return out;
  }
};

static const char* const API_PATHS[] = {
  "/api/ble", "/api/bledev", "/api/wifi", "/api/sub", "/api/sens", "/api/snd", "/api/loc2x", "/api/pagever", "/api/settings",
  "/api/set", "/api/label", "/api/act", "/api/pckey", "/api/loc", "/api/info", "/api/ver",
};

// true: path is known (reply in o)
static bool apiHandle(const char* path, const ApiArgs& A, String& o) {
  auto is = [&](const char* p) { return !strcmp(path, p); };
  // while the Web section is closed (PC app over USB) do not touch the radio, only settings and info
  if (!s_on && !(is("/api/pagever") || is("/api/settings") || is("/api/set") || is("/api/label") || is("/api/act") ||
                 is("/api/pckey") || is("/api/loc") || is("/api/info") || is("/api/ver"))) {
    o = "{\"ok\":false,\"err\":\"open Web on the stick\"}"; return true;
  }
  if (is("/api/ble")) { bleJson(o); return true; }
  if (is("/api/bledev")) { bleDevJson((uint32_t)strtoul(A.get("id").c_str(), nullptr, 10), o); return true; }
  if (is("/api/wifi")) { o = s_wifiJson; return true; }
  if (is("/api/sub")) {
    s_subMs = millis();
    s_subOn = subWebUse(A.get("m").toInt(), A.has("b") ? A.get("b").toInt() : 1);
    subJson(o); return true;
  }
  if (is("/api/sens")) {                           // sensors 433 (poll): enables packet decoding, like /api/sub?m=2
    s_subMs = millis();
    s_subOn = subWebUse(G_SENSORS, A.has("b") ? A.get("b").toInt() : 1);
    sensJson(o, millis()); return true;
  }
  if (is("/api/snd")) { s_sndMs = millis(); s_sndOn = audioWebUse(); audioJson(o); return true; }
  if (is("/api/loc2x")) {
    s_locMs = millis();
    s_locOn = locWebUse();
    if (A.get("a") == "cal") { locCalibrate(o); return true; }
    if (A.has("base")) locSetBase(A.get("base").toFloat());
    locJson(o);
    return true;
  }
  // tablet app version: the app compares it and fetches /full if the device has a newer one
  if (is("/api/pagever")) { o = "{\"sha\":\""; o += TABLET_HTML_SHA; o += "\",\"gz\":"; o += TABLET_GZ_LEN; o += '}'; return true; }
  // tablet as a remote: device settings, device labels, actions
  if (is("/api/settings")) { settingsJson(o); return true; }
  if (is("/api/set")) {
    String k = A.get("k"), v = A.get("v");
    { char c[48]; snprintf(c, sizeof c, "api set %s=%s", k.c_str(), v.c_str()); crumb(c); }
    settingsSetKey(k.c_str(), v.c_str(), o); return true;
  }
  if (is("/api/label")) {
    bool stable = false;
    bool ok = bleLabelSet((uint32_t)strtoul(A.get("id").c_str(), nullptr, 10), A.get("n").c_str(), stable);
    o = ok ? (stable ? "{\"ok\":true,\"stable\":true}" : "{\"ok\":true,\"stable\":false}") : "{\"ok\":false,\"err\":\"device not found\"}";
    return true;
  }
  if (is("/api/act")) {
    String a = A.get("a"); int v = A.get("v").toInt(); bool ok = true;
    if (a == "screen") { g_screenOff = !v; applyBrightness(); }   // turn the device screen off / on (work continues)
    else if (a == "leds" && !v) ledsOff();
    else ok = false;
    o = ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"unknown action\"}"; return true;
  }
  // music remote: send a media key to the laptop over USB
  if (is("/api/pckey")) { o = pcKey(A.get("k").c_str()) ? "{\"ok\":true}" : "{\"ok\":false}"; return true; }
  // the tablet app sends its GPS: the device's sky and globe are computed from there
  if (is("/api/loc")) {
    float lat = A.get("lat").toFloat(), lon = A.get("lon").toFloat();
    bool ok = A.has("lat") && A.has("lon") && fabsf(lat) <= 90 && fabsf(lon) <= 180 && (lat != 0 || lon != 0);
    if (ok) setGpsCity(lat, lon);
    o = ok ? "{\"ok\":true}" : "{\"ok\":false}"; return true;
  }
  if (is("/api/info")) {
    o.reserve(400);
    o = "{\"up\":"; o += millis() / 1000;
    o += ",\"bat\":{\"ok\":"; o += g_bat.ok ? "true" : "false";
    o += ",\"soc\":"; o += g_bat.soc; o += ",\"mv\":"; o += g_bat.mv; o += ",\"ma\":"; o += g_bat.ma;
    o += ",\"t\":"; o += String(g_bat.tempC, 1); o += ",\"tte\":"; o += g_bat.tte; o += ",\"ttf\":"; o += g_bat.ttf;
    o += ",\"rem\":"; o += g_bat.remain; o += ",\"full\":"; o += g_bat.full;
    o += ",\"usb\":"; o += g_bat.bus ? "true" : "false"; o += ",\"chg\":"; o += g_bat.chg; o += '}';
    o += ",\"chip\":"; o += String(temperatureRead(), 1);
    o += ",\"heap\":"; o += ESP.getFreeHeap() / 1024; o += ",\"psram\":"; o += ESP.getFreePsram() / 1024;
    o += ",\"cli\":"; o += s_on && s_ok ? (int)WiFi.softAPgetStationNum() : 0;
    o += ",\"nets\":"; o += s_netCount;
    o += ",\"build\":\"" __DATE__ " " __TIME__ "\"";
    o += ",\"theme\":\""; o += g_theme == THEME_KURENAI ? "kurenai" : "denko"; o += '"';
    o += ",\"ver\":\"" FW_VERSION "\",\"lang\":\""; o += LANG_CODE; o += '"';
    o += ",\"rst\":"; jsonStr(o, crumbLastReason()); o += ",\"rstStep\":"; jsonStr(o, crumbLastStep());
    o += ",\"ssid\":\""; o += s_ssid; o += "\"}";
    return true;
  }
  if (is("/api/ver")) {
    const esp_partition_t* run = esp_ota_get_running_partition();
    o = "{\"build\":\"" __DATE__ " " __TIME__ "\",\"slot\":\""; o += run ? run->label : "?";
    o += "\",\"max\":"; o += (uint32_t)ESP.getFreeSketchSpace(); o += '}';
    return true;
  }
  return false;
}

static void webStart() {
  if (s_on) return;
  wifiRadioOff();                                  // the Wi-Fi section may have left the radio in its own mode
  bleLowDuty(true);
  s_bleWanted = -1;                                // webTick enables Bluetooth only if it is viewed (it shares airtime with the AP)
  WiFi.mode(WIFI_AP_STA);
  makeNames();                                     // MAC is known once Wi-Fi is on
  s_ok = WiFi.softAP(s_ssid, s_pass, 6, 0, 4);
  if (!s_ok) { s_on = true; return; }
  IPAddress ip = WiFi.softAPIP();
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", ip);                        // any address -> this device (the page opens by itself)
  s_srv = new WebServer(80);
  const char* hdrs[] = { "If-None-Match", "Content-Length" };
  s_srv->collectHeaders(hdrs, 2);
  // entry: "/" is the launcher (splash, LITE / FULL choice, same app as /full);
  // "/lite" is the light phone page; "/full" opens the full scene directly.
  // Old /phone and /tablet addresses are kept and lead to the same places.
  static auto sendApp = [] {                       // tablet app: one gzip file with ETag + chunked WDT-safe send
    s_reqs++;
    char etag[72];
    snprintf(etag, sizeof(etag), "\"%s\"", TABLET_HTML_SHA);
    s_srv->client().setNoDelay(true);
    s_srv->sendHeader("ETag", etag);
    s_srv->sendHeader("Cache-Control", "no-cache");
    if (s_srv->header("If-None-Match") == etag) {
      s_srv->send(304, "text/plain", "");
      return;
    }
    s_srv->sendHeader("Content-Encoding", "gzip");
    s_srv->setContentLength(TABLET_GZ_LEN);
    s_srv->send(200, "text/html", "");
    const char* p = (const char*)TABLET_GZ;
    size_t rem = TABLET_GZ_LEN;
    while (rem > 0 && s_srv->client().connected()) {
      size_t chunk = rem > 8192 ? 8192 : rem;
      s_srv->sendContent_P(p, chunk);
      p += chunk; rem -= chunk;
      crumbFeed();
    }
  };
  s_srv->on("/", [] { sendApp(); });
  s_srv->on("/full", [] { sendApp(); });
  s_srv->on("/tablet", [] { sendApp(); });
  // phone page: language follows the device (<html lang> set when served)
  static auto sendLite = [] {
    s_reqs++;
    size_t a = strlen_P(WEB_PAGE_HEAD), b = strlen_P(WEB_PAGE);
    s_srv->setContentLength(a + 2 + b);
    s_srv->send(200, "text/html", "");
    s_srv->sendContent_P(WEB_PAGE_HEAD, a);
    s_srv->sendContent(LANG_CODE);
    s_srv->sendContent_P(WEB_PAGE, b);
  };
  s_srv->on("/lite", [] { sendLite(); });
  s_srv->on("/phone", [] { sendLite(); });
  for (const char* path : API_PATHS) s_srv->on(path, [path] { HttpArgs a; String o; apiHandle(path, a, o); sendJson(o); });
  s_srv->on("/update", HTTP_POST, otaDone, otaUpload);
  s_srv->onNotFound([] {                           // phone connectivity checks -> redirect to the page
    s_srv->sendHeader("Location", "http://192.168.4.1/", true);
    s_srv->send(302, "text/plain", "");
  });
  s_srv->begin();
  s_sseSrv = new NetworkServer(81);                  // side channel: tablet stream
  s_sseSrv->begin();
  s_sseSrv->setNoDelay(true);
  s_scanMs = millis();                             // first channel in a couple of seconds
  s_on = true;
}

static void webStop() {
  if (!s_on) return;
  otaFreeHead();
  if (s_srv) { s_srv->stop(); delete s_srv; s_srv = nullptr; }
  for (auto& k : s_sse) { if (k.on) k.c.stop(); k = SseCli(); }
  if (s_sseSrv) { s_sseSrv->end(); delete s_sseSrv; s_sseSrv = nullptr; }
  s_dns.stop();
  WiFi.scanDelete();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  bleRadioOff();
  bleLowDuty(false);
  if (s_subOn) { subWebStop(); s_subOn = false; }
  if (s_sndOn) { audioWebStop(); s_sndOn = false; }
  if (s_irOn) { irStop(); s_irOn = false; }
  if (s_nfcOn) { nfcStop(); s_nfcOn = false; }
  if (s_locOn) { locWebStop(); s_locOn = false; }
  s_nNets = 0; s_netCount = 0; s_scanned = false;
  s_wifiJson = "{\"scanned\":false,\"nets\":[]}";
  s_on = false;
  s_tab = "";
}

static void webTick(uint32_t now) {
  crumb("web tick");
  if (!s_on) return;
  if (s_ok) {                                      // AP is up (USB works without it)
    s_dns.processNextRequest();
    crumb("web http"); s_srv->handleClient();
    scanCollect();
  }
  crumb("web sse"); sseTick(millis());
  bool sBle, sWifi, sSnd, sWx; int subM, subB;
  int nSse = sseClients(sBle, sWifi, sSnd, subM, subB, sWx);
  // tablet radio weather: Bluetooth + CC1101 433.92 monitor + microphone (Wi-Fi left alone, it would interfere)
  if (nSse && sWx) {
    sBle = true; sSnd = true;
    if (subM < 0) { subM = 1; subB = 1; }             // nobody watches Sub-GHz separately: monitor (mode 1) on 433.92 (band 1)
  }
  // 433 sensors: CC1101 in packet-decoding mode (the monitor decodes too, leave it alone)
  bool sSens = false;
  for (auto& k : s_sse) if (k.on) sSens |= k.sens;
  if (nSse && sSens && subM != G_MONITOR && subM != G_SENSORS) { if (subM < 0) subB = 1; subM = G_SENSORS; }
  if (nSse && sSnd) { s_sndMs = millis(); if (!s_sndOn) s_sndOn = audioWebUse(); }   // tablet watches sound
  if (nSse && subM >= 0) { s_subMs = millis(); s_subOn = subWebUse(subM, subB); }    // tablet watches Sub-GHz
  // only what the open tab needs runs, the rest stays silent:
  //  Bluetooth: Overview and Bluetooth; network scan: Overview and Wi-Fi; CC1101 and mic: their own tabs only
  // nobody watching -> radio off
  bool wantBle = tabIs("overview") || tabIs("bluetooth");
  if (nSse) wantBle = sBle;                          // tablet connected: its subscriptions decide
  if (nSse && sWx) wxUpdate(millis());               // recompute weather once a second (sources enabled above)
  if ((int8_t)wantBle != s_bleWanted) { s_bleWanted = wantBle; if (wantBle) bleRadioOn(); else bleRadioOff(); }
  bool wantScan = tabIs("overview") || tabIs("wi-fi");
  if (nSse) wantScan = sWifi;
  // CC1101 and mic run only while the phone views their tab
  // re-read the time: the request may arrive after now was taken, and now - s_sndMs would wrap
  // to a huge number (the mic switched off at once)
  uint32_t t = millis();
  if (s_subOn) { if ((int32_t)(t - s_subMs) > 4000) { subWebStop(); s_subOn = false; } else subWebTick(t); }
  if (s_sndOn) { if ((int32_t)(t - s_sndMs) > 4000) { audioWebStop(); s_sndOn = false; } else audioWebTick(t); }
  // IR, NFC and Locator 2x: run while a tablet is subscribed (+4 s), then sleep
  bool sIr = false, sNfc = false, sLoc = false;
  for (auto& k : s_sse) if (k.on) { sIr |= k.ir; sNfc |= k.nfc; sLoc |= k.loc; }
  if (sIr)  { s_irMs = t;  if (!s_irOn)  { irStart();  s_irOn = true; } }
  if (sNfc) { s_nfcMs = t; if (!s_nfcOn) { nfcStart(); s_nfcOn = true; } }
  if (sLoc) { s_locMs = t; if (!s_locOn) { s_locOn = locWebUse(); } }
  if (s_irOn)  { if ((int32_t)(t - s_irMs) > 4000)  { irStop();     s_irOn = false; }  else { crumb("web ir");  irTick(t); } }
  if (s_nfcOn) { if ((int32_t)(t - s_nfcMs) > 4000) { nfcStop();    s_nfcOn = false; } else { crumb("web nfc"); nfcTick(t); } }
  if (s_locOn) {
    if ((int32_t)(t - s_locMs) > 4000) { locWebStop(); s_locOn = false; }
    else { crumb("web loc"); locTick(t); wantScan = false; } // pause 13-ch Wi-Fi hopping so Channel 6 ESP-NOW has 0% loss
  }
  if (s_ok && wantScan && now - s_scanMs > SCAN_STEP_MS && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {   // one channel at a time so the AP does not vanish
    s_scanMs = now;
    WiFi.scanNetworks(true, true, false, 90, s_scanCh);   // async, 90 ms per channel
    s_scanCh = s_scanCh >= 13 ? 1 : s_scanCh + 1;
  }
}


// ============================================================
//  PC app over a USB cable (tools/desktop). Same wire as the PC monitor; app lines start with "@"
//  (passed in by pc.cpp), everything else is still the PC monitor.
//  PC -> device:  @W want=...&sm=&sb=  subscribe (like ?want= for the tablet; repeat every 2 s = "I'm here")
//                 @Q <id> /api/...?...  request, same as the tablet sends over HTTP
//                 @G <id>               send the page (gzip, base64 in chunks)
//  device -> PC:  @E <event> <json>     stream events, same JSON as the tablet's SSE
//                 @R <id> <json>        reply to @Q
//                 @g <id> begin <bytes> <sha> / @g <id> <offset> <base64> / @g <id> end
//                 @X web                the Web section is closed on the device, open it
//  No reply for 6 s: the app is closed, radio switches off by itself (like the tablet).
// ============================================================
static uint32_t s_linkXMs = 0;
static uint32_t s_linkWMs = 0;                    // last "@W" from the app (new connection -> open Web)
static int32_t  s_pgId = -1;                      // page transfer: to whom and from where
static size_t   s_pgOff = 0;

void webLinkLine(char* s) {
  uint32_t now = millis();
  char cmd = s[1];
  char* rest = s[2] == ' ' ? s + 3 : s + 2;
  if (cmd == 'W') {
    // new app connection (no "@W" for over 8 s): open the Web section;
    // if Web was closed by hand while the app is connected, do not force it, only hint with "@X"
    bool fresh = !s_linkWMs || msSince(now, s_linkWMs) > 8000 || (now < 15000 && !s_on);
    s_linkWMs = now ? now : 1;
    if (!s_on && fresh) { powerTouch(); navOpen(&webSection); }
    if (!s_on) {                                   // nothing to subscribe to: section closed
      if (msSince(now, s_linkXMs) > 1500 || !s_linkXMs) { s_linkXMs = now ? now : 1; serOutLine("@X web"); }
      return;
    }
    SseCli& k = s_sse[USB_SLOT];
    String q(rest);
    if (!k.on || k.wantQ != q) {                   // new subscription (or the tab changed)
      bool had = k.on; uint32_t keepSt = k.tSt; bool keepSat = k.satSent;
      k = SseCli(); k.usb = true; k.on = true; k.wantQ = q;
      parseWant(k, q + " ");
      if (had) { k.tSt = keepSt; k.satSent = keepSat; }
      s_reqs++;
    }
    k.seenMs = now;
    return;
  }
  if (cmd == 'Q') {
    char* sp = strchr(rest, ' '); if (!sp) return;
    *sp = 0; const char* id = rest; char* path = sp + 1;
    char* qm = strchr(path, '?'); String query;
    if (qm) { *qm = 0; query = qm + 1; }
    QueryArgs a(query); String o;
    if (!apiHandle(path, a, o)) o = "{\"ok\":false,\"err\":\"unknown\"}";
    size_t il = strlen(id);
    if (serOutBegin(4 + il + o.length(), false)) { serOutPart("@R ", 3); serOutPart(id, il); serOutPart(" ", 1); serOutPart(o.c_str(), o.length()); serOutEnd(); }
    if (s_sse[USB_SLOT].on) s_sse[USB_SLOT].seenMs = now;
    s_reqs++;
    return;
  }
  if (cmd == 'G') {
    s_pgId = atoi(rest); s_pgOff = 0;
    char b[96]; snprintf(b, sizeof b, "@g %d begin %u %s", (int)s_pgId, (unsigned)TABLET_GZ_LEN, TABLET_HTML_SHA);
    serOutLine(b);
  }
}

void webLinkPump() {
  // page: 1.5 KB chunks while the queue is short (stream data is not delayed)
  while (s_pgId >= 0 && serOutQueued() < 8192) {
    size_t n = TABLET_GZ_LEN - s_pgOff; if (n > 1536) n = 1536;
    if (!n) { char b[32]; snprintf(b, sizeof b, "@g %d end", (int)s_pgId); serOutLine(b); s_pgId = -1; break; }
    static unsigned char enc[2100]; size_t olen = 0;
    mbedtls_base64_encode(enc, sizeof enc, &olen, (const unsigned char*)TABLET_GZ + s_pgOff, n);
    char head[40]; int hl = snprintf(head, sizeof head, "@g %d %u ", (int)s_pgId, (unsigned)s_pgOff);
    if (hl < 0) { s_pgId = -1; break; }
    if (hl >= (int)sizeof(head)) hl = (int)sizeof(head) - 1;
    if (!serOutBegin((size_t)hl + olen, false)) break;
    serOutPart(head, hl); serOutPart((const char*)enc, olen); serOutEnd();
    s_pgOff += n;
  }
  serOutPump();
}

// screen: link animation while a tablet is connected; wheel or press shows QR and password
// (to connect someone else or recall the password), animation returns after 30 s idle
static bool     s_showInfo = false;
static uint32_t s_infoMs = 0;
static void webDraw(uint32_t now) {
  WebView v = { s_ssid, s_pass, "192.168.4.1", s_ok ? (int)WiFi.softAPgetStationNum() : 0, s_reqs, s_ok, s_tab };
  int tablets = 0; bool b = false, w = false, sn = false, su = false, pc = false, wx = false, ir = false, nf = false;
  for (auto& k : s_sse) if (k.on) { tablets++; b |= k.ble; w |= k.wifi; sn |= k.snd; su |= k.sub; pc |= k.pc; wx |= k.wx; ir |= k.ir; nf |= k.nfc; }
  if (s_showInfo && msSince(now, s_infoMs) > 30000) s_showInfo = false;
  if (!tablets || s_showInfo) { drawWebScreen(v, s_qr, s_qrN); return; }
  // speed: bytes over the last second (smoothed)
  static uint32_t lastBytes = 0, lastMs = 0; static float bps = 0;
  if (!lastMs) { lastMs = now; lastBytes = s_sseBytes; }
  uint32_t dt = msSince(now, lastMs);
  if (dt >= 250) { bps = bps * 0.6f + 0.4f * (s_sseBytes - lastBytes) * 1000.0f / dt; lastBytes = s_sseBytes; lastMs = now; }
  char st[72] = "";
  if (s_sse[USB_SLOT].on) strcat(st, "USB  ");    // PC app over cable
  if (b) strcat(st, "BLE  "); if (w) strcat(st, "WIFI  "); if (su) strcat(st, "SUB  "); if (sn) strcat(st, "SND  "); if (pc) strcat(st, "PC  "); if (wx) strcat(st, "WX  "); if (ir) strcat(st, "IR  "); if (nf) strcat(st, "NFC  ");
  if (!st[0]) strcpy(st, L("STATUS", "СТАТУС"));
  WebLink l = { tablets, st, (uint32_t)bps, s_sseBytes };
  drawWebLink(l, now);
}

static void webSubtitle(char* out, size_t n) {
  if (s_on && s_ok) snprintf(out, n, L("%d online", "%d на связи"), (int)WiFi.softAPgetStationNum());
  else              snprintf(out, n, "%s", L("apps & phone", "приложения"));
}
static void webEnter() { webStart(); }
static void webLeave() { webStop(); ledsOff(); }
static void webClick() { s_showInfo = !s_showInfo; s_infoMs = millis(); }
static void webRotate(int) { s_showInfo = !s_showInfo; s_infoMs = millis(); }

const Section webSection = {
  LP("Web", "Веб"), 90, 200, 230, iconPhone, webSubtitle,
  webEnter, webLeave,
  0, nullptr, nullptr, nullptr, nullptr,
  webClick, webRotate, nullptr, webTick, webDraw,
};
