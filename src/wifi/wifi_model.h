// ============================================================
//  wifi_model.h — Wi-Fi data shared by logic (wifi.cpp) and screens (wifi_screens.cpp).
//  Outside the section only wifi.h is needed.
// ============================================================
#pragma once
#include <Arduino.h>
#include <map>
#include <vector>

enum { W_NETS = 0, W_CHANNELS, W_ACTIVITY, W_CLIENTS, W_FIND, W_NUM };
static const int CH_MAX = 13;                 // 2.4 GHz channels 1-13

// access point
struct Ap {
  uint8_t  bssid[6];
  char     ssid[33];
  int8_t   rssi, rssiS, rmin, rmax;
  uint8_t  ch, enc;
  int8_t   sec;        // secondary channel (40 MHz): +1 above, -1 below, 0 = plain 20 MHz
  uint8_t  gen;        // 6 = Wi-Fi 6 (ax), 4 = Wi-Fi 4 (n), 3 = 11g, 1 = 11b
  bool     hotspot;    // looks like a phone hotspot
  const char* vendor;  // vendor by OUI (or nullptr)
  uint32_t t, first;
};
struct Sta { uint64_t bssid; uint32_t t; uint16_t pkts; };   // device in a network (count + activity)
struct ProbeSsid { char ssid[25]; uint32_t t; };
#define PROBE_SSID_MAX 12

extern int  g_wMode;
extern bool g_wEverScanned, g_scanning, g_prom;
extern std::map<uint64_t, Ap> g_aps;
extern std::map<uint64_t, Sta> g_stas;
extern std::map<uint64_t, uint32_t> g_probes;
extern ProbeSsid g_probeSsids[PROBE_SSID_MAX];
extern int       g_nProbeSsids;

// list and details
extern std::vector<uint64_t> g_wOrder;
extern uint64_t g_wSel;
extern int      g_wTop;
extern bool     g_wDetail;
#define W_SPARK_N 60
extern int8_t   g_wSpark[W_SPARK_N];
extern int      g_wSparkN;
// FIND
#define F_SPARK_N 100
extern int8_t   g_fSpark[F_SPARK_N];
extern int      g_fSparkN;
extern uint32_t g_fLastHit;
// ACTIVITY
extern int      g_hopCh;
extern float    g_ppsMgmt[CH_MAX + 1], g_ppsData[CH_MAX + 1], g_noise[CH_MAX + 1];
#define ACT_HIST_N 120
extern float    g_actHist[ACT_HIST_N];
extern int      g_actHistN;

// helpers (wifi.cpp)
uint64_t    macKey(const uint8_t* m);
const char* encName(uint8_t e);
const char* genName(uint8_t g);
const char* ssidOr(const Ap& a);                 // name or "(hidden)"
float       apCenter(const Ap& a);               // band center, in channels
float       apHalf(const Ap& a);                 // half-width: 20 MHz -> 2, 40 MHz -> 4
float       interference(const Ap& a, int k);    // how much the network interferes with channel k (0..1)
int         bestChannel();                       // best of 1 / 6 / 11
int         clientsOf(uint64_t bssid);
uint64_t    findTarget();                        // target network for FIND
const char* ouiName(const uint8_t* mac);         // vendor by MAC (or nullptr)

// screens (wifi_screens.cpp)
void drawNets(uint32_t now);
void drawNetDetail(uint32_t now);
void drawChannels(uint32_t now);
void drawActivity(uint32_t now);
void drawClients(uint32_t now);
void drawFind(uint32_t now);
