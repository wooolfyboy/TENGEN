// ============================================================
//  ble_model.h — Bluetooth data shared by the logic (ble_core.cpp)
//  and the screens (ble_screens.cpp). Outside the section only ble/ble.h is used.
// ============================================================
#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include "ble/devtypes.h"
#include "ble/sensors.h"

static const uint32_t REMEMBER_MS = 30000;          // how long a silent address is remembered
static const uint32_t SENSOR_REMEMBER_MS = 300000;  // sensors transmit rarely: remember 5 min
static const size_t   MAX_SENSORS = 24;
static const size_t   MAX_DEVS    = 600;            // memory guard
// address-rotation merge: the old address must be silent for at least STALE_MIN
// and for no longer than STALE_MAX, with a similar signal strength
static const uint32_t STALE_MIN   = 1500;
static const uint32_t STALE_MAX   = 12000;
static const int      MERGE_DB    = 10;
// list: "sticky" order
static const int      ORDER_HYST_DB = 8;            // swap only if the difference is > 8 dB
static const uint32_t ORDER_EVERY   = 2000;         // how often to refresh the order
static const uint32_t ORDER_FREEZE  = 4000;         // order freezes after a wheel touch

enum { M_OVERVIEW = 0, M_SCOPE, M_CITY, M_NEAREST, M_LIST, M_WEATHER, NUM_MODES };
enum { V_PRIV = 0, V_OTHER = 1 };                   // 2.. = brands from BRANDS (vendors.h)
enum { A_PUBLIC = 0, A_STATIC, A_ROTATING, A_NONRES };   // address kinds

// Dev = one MAC address. ent = id of the "physical" device;
// shared by all addresses of one phone (after MAC rotation).
struct Dev {
  uint32_t t;        // last heard
  uint32_t first;    // first heard (after merging)
  uint32_t ent;      // id of the physical device
  uint32_t sig;      // advertisement fingerprint
  uint32_t pkts;     // packets caught
  int8_t   rssi;     // last signal
  int8_t   rssiS;    // smoothed signal (for sorting)
  float    rssiF;    // sub-decibel floating-point smoothed signal (for Locator 2x)
  int8_t   rmin, rmax;
  uint8_t  cat;      // brand (V_* / index in BRANDS)
  uint8_t  vkind;    // how the vendor was found: 0 none, 1 Company ID, 2 service UUID
  uint8_t  vscore;   // confidence (3 brand, 2 other, 1 chip maker)
  uint16_t vcode;    // the Company ID or UUID itself
  uint8_t  type;     // device type (DevType)
  uint8_t  akind;    // address kind (A_*)
  bool     rnd;      // random, rotating address
  bool     merged;   // already became a "predecessor" of a new address
  bool     inRange;  // within range threshold (with hysteresis)
  bool     hasCid;
  uint16_t cid;      // Company ID
  uint16_t appearance;
  uint8_t  mac[6];   // address (lowest byte first)
  uint8_t  mfr[6];   // first bytes of manufacturer data
  uint8_t  mfrLen;   // its full length
  uint16_t uuids[4]; // services
  uint8_t  nU;
  char     name[20];
};
// Ent = physical device (aggregate over its addresses)
struct Ent {
  uint32_t t, first, pkts;
  int      rssi, rssiS;
  uint8_t  cat, type, nMacs;
  uint8_t  vkind, vscore;
  uint16_t vcode;
  uint64_t key;      // key of the freshest address
  char     name[20];
  char     label[21]; // label set by the user on the tablet (UTF-8, may be empty)
};
// weather sensor
struct Sensor {
  uint32_t t;
  int8_t   rssi;
  SensorReading r;
  uint8_t  mac[6];
  char     name[20];
};
struct Blip { int rssi; uint32_t h; uint8_t cat; uint8_t type; char label[21]; };   // point on SCOPE / CITY (label = user label)

// --- TIMELINE history ---
#define HIST_N 150
struct Sample { int count; float base; int8_t ev; };
extern Sample g_hist[HIST_N];
extern int    g_histLen;

// --- section state ---
extern volatile int g_mode;            // section screen (M_*)
extern bool  g_bleActive;              // Bluetooth is scanning now
extern int   g_lastCount;              // physical devices (within range)
extern int   g_lastRaw;                // MAC addresses (unmerged)
extern int   g_sensCount;              // weather sensors

// --- list and details ---
extern std::vector<uint32_t> g_order;  // row order (device ids)
extern uint32_t g_sel;                 // selected device
extern int      g_top;                 // first visible row
extern bool     g_detail;              // details are open
#define SPARK_N 60
extern int8_t   g_spark[SPARK_N];      // signal history of the selected one (a point every 0.5 s)
extern int      g_sparkN;

// ---------- screens (ble_screens.cpp) ----------
float       estDistance(int rssi);
void drawOverview(int count, int raw, const int* typeCnt);
void drawScope(const std::vector<Blip>& bl);
void drawCity(const std::vector<Blip>& bl, uint32_t now);   // 3D: devices are towers
void cityTurn(int dir);                                      // wheel: turn the camera
void drawNearest(bool have, int rssi, const Ent& best, int count);
void drawList(const std::map<uint32_t, Ent>& ents);
void drawDetail(const Dev& d, const Ent& e, bool inMap, uint32_t now);
void drawWeather(const std::vector<Sensor>& ss, uint32_t now);
