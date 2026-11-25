// ============================================================
//  ble_city.cpp — CITY screen of the Bluetooth section (3D "city" of devices).
//  Split out of ble_screens.cpp: own 3D code and animation.
// ============================================================
#include <algorithm>
#include "core/lang.h"
#include "core/timeutil.h"
#include "ble/ble_model.h"
#include "ble/vendors.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/settings.h"
#include "audio/audio.h"

// ============================================================
//  CITY: devices are towers on a round plate. Closer = nearer the
//  centre and taller; colour = signal strength, tags (trackers) are
//  orange. The camera slowly orbits, the wheel turns it; new towers grow.
// ============================================================
static float s_yaw = 0.6f, s_yawTarget = 0.6f;
struct CityT { float rssiAvg = 0, target = 0, pct = 0, h = 0; uint8_t type = 0; bool alive = false; char label[21] = ""; };
static std::map<uint32_t, CityT> s_city;             // towers: smoothed values and animation

void cityTurn(int dir) { s_yawTarget += dir * 0.26f; }

void drawCity(const std::vector<Blip>& bl, uint32_t now) {
  const int W = spr.width();
  g_fastFrame = true;
  char buf[32];
  spr.fillSprite(C_BG());

  s_yawTarget += 0.004f;                              // slow orbit
  s_yaw += (s_yawTarget - s_yaw) * 0.2f;
  const float cy_ = cosf(s_yaw), sy_ = sinf(s_yaw);
  const float pitch = 0.62f, cp = cosf(pitch), sp = sinf(pitch);
  const float F = 300, dist = 3.2f;
  const int cx = W / 2, cyS = 104;
  // world: x, z = plate (radius 1), y = up
  auto proj = [&](float x, float y, float z, float& sx, float& sy, float& d) {
    float xr = x * cy_ - z * sy_, zr = x * sy_ + z * cy_;
    float vy = y * cp + zr * sp, vz = -y * sp + zr * cp;
    d = dist + vz;
    sx = cx + xr * F / d; sy = cyS - vy * F / d;
  };

  // plate: rings, rays and the scan wave (LUT avoids ~190 sin/cos calls per frame)
  static float s_c48[49], s_s48[49], s_c40[41], s_s40[41];
  static bool s_cityLut = false;
  if (!s_cityLut) {
    for (int i = 0; i <= 48; i++) { float a = i * 2 * PI / 48; s_c48[i] = cosf(a); s_s48[i] = sinf(a); }
    for (int i = 0; i <= 40; i++) { float a = i * 2 * PI / 40; s_c40[i] = cosf(a); s_s40[i] = sinf(a); }
    s_cityLut = true;
  }
  const uint16_t ring = UI_LINE();
  for (int k = 1; k <= 3; k++) {
    float r = k / 3.0f, px = 0, py = 0, d;
    for (int i = 0; i <= 48; i++) {
      float sx, sy;
      proj(s_c48[i] * r, 0, s_s48[i] * r, sx, sy, d);
      if (i) spr.drawLine(px, py, sx, sy, k == 3 ? C_ACC() : ring);
      px = sx; py = sy;
    }
  }
  float cx0, cy0, cd0; proj(0, 0, 0, cx0, cy0, cd0);
  for (int i = 0; i < 8; i++) {
    float x1, y1, d;
    proj(s_c48[i * 6], 0, s_s48[i * 6], x1, y1, d);
    spr.drawLine(cx0, cy0, x1, y1, ring);
  }
  {
    float wr = (now % 2200) / 2200.0f, px = 0, py = 0, d;     // wave from the centre
    uint16_t wc = scale565(C_ACC(), (int)(70 * (1 - wr)) + 10);
    for (int i = 0; i <= 40; i++) {
      float sx, sy;
      proj(s_c40[i] * wr, 0, s_s40[i] * wr, sx, sy, d);
      if (i) spr.drawLine(px, py, sx, sy, wc);
      px = sx; py = sy;
    }
  }

  // towers: sample data every 1.5 s (BLE signal is noisy), in between
  // height and position ease toward the new values
  struct Tower { float x, z, h, depth; uint16_t col; bool alert; int rssi; uint8_t type; const char* label; };
  static uint32_t lastPick = 0;
  if (now - lastPick >= 1500 || s_city.empty()) {
    lastPick = now;
    static std::vector<int> idx;
    idx.resize(bl.size());
    for (size_t i = 0; i < bl.size(); i++) idx[i] = (int)i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return bl[a].rssi > bl[b].rssi; });
    if (idx.size() > 48) idx.resize(48);
    for (auto& kv : s_city) kv.second.alive = false;
    for (int i : idx) {
      const Blip& b = bl[i];
      CityT& c = s_city[b.h];
      if (c.rssiAvg == 0) { c.rssiAvg = b.rssi; c.pct = c.h = 0; }
      c.rssiAvg = c.rssiAvg * 0.5f + b.rssi * 0.5f;      // extra averaging between samples
      c.target = constrain((c.rssiAvg + 95) / 50.0f, 0.0f, 1.0f);
      c.type = b.type; c.alive = true; strlcpy(c.label, b.label, sizeof(c.label));
      if (c.pct == 0) c.pct = c.target;                  // a new tower appears in place and grows
    }
  }
  static std::vector<Tower> tw;                        // static: no reallocation every frame
  tw.clear();
  for (auto it = s_city.begin(); it != s_city.end();) {
    CityT& c = it->second;
    if (c.alive) c.pct += (c.target - c.pct) * 0.06f;
    float hTarget = c.alive ? 0.06f + 0.62f * c.pct * c.pct : 0.0f;
    c.h += (hTarget - c.h) * 0.08f;
    if (!c.alive && c.h < 0.01f) { it = s_city.erase(it); continue; }   // gone: sinks and disappears
    float ang = (it->first % 360) * PI / 180.0f, r = 0.95f - 0.8f * c.pct;
    Tower t;
    t.x = cosf(ang) * r; t.z = sinf(ang) * r; t.h = c.h;
    float sx, sy; proj(t.x, 0, t.z, sx, sy, t.depth);
    t.alert = (c.type == T_TRACKER);
    t.col = t.alert ? C_ALERT() : heatV(0.2f + 0.8f * c.pct);
    t.rssi = (int)lroundf(c.rssiAvg); t.type = c.type; t.label = c.label;
    tw.push_back(t);
    ++it;
  }
  std::sort(tw.begin(), tw.end(), [](const Tower& a, const Tower& b) { return a.depth > b.depth; });

  const float S = 0.045f;                             // tower half-width
  const float CX[4] = { -S, S, S, -S }, CZ[4] = { -S, -S, S, S };
  for (const Tower& t : tw) {
    float bx[4], by[4], tx[4], ty[4], dd[4], d;
    for (int k = 0; k < 4; k++) {
      proj(t.x + CX[k], 0, t.z + CZ[k], bx[k], by[k], dd[k]);
      proj(t.x + CX[k], t.h, t.z + CZ[k], tx[k], ty[k], d);
    }
    // side faces: far before near; light from the side, faces differ in brightness
    int order[4] = { 0, 1, 2, 3 };
    std::sort(order, order + 4, [&](int a, int b) {
      return dd[a] + dd[(a + 1) % 4] > dd[b] + dd[(b + 1) % 4];
    });
    for (int o = 0; o < 4; o++) {
      int a = order[o], b = (a + 1) % 4;
      int light = (a % 2 == 0) ? 55 : 35;
      uint16_t fc = scale565(t.col, light);
      spr.fillTriangle(bx[a], by[a], bx[b], by[b], tx[b], ty[b], fc);
      spr.fillTriangle(bx[a], by[a], tx[b], ty[b], tx[a], ty[a], fc);
    }
    // roof and edges
    spr.fillTriangle(tx[0], ty[0], tx[1], ty[1], tx[2], ty[2], t.col);
    spr.fillTriangle(tx[0], ty[0], tx[2], ty[2], tx[3], ty[3], t.col);
    for (int k = 0; k < 4; k++) spr.drawLine(tx[k], ty[k], tx[(k + 1) % 4], ty[(k + 1) % 4], C_HOT());
  }
  // labels of the three nearest: type and signal above the roof
  spr.setTextDatum(BC_DATUM);
  static std::vector<const Tower*> top;
  top.clear();
  for (const Tower& t : tw) top.push_back(&t);
  std::sort(top.begin(), top.end(), [](const Tower* a, const Tower* b) { return a->rssi > b->rssi; });
  int lx[3], ly[3], lw[3];
  for (size_t n = 0; n < top.size() && n < 3; n++) {
    float sx, sy, d; proj(top[n]->x, top[n]->h, top[n]->z, sx, sy, d);
    bool lab = top[n]->label[0];                        // user label replaces the type (Cyrillic uses its own font)
    snprintf(buf, sizeof(buf), "%s %d", lab ? top[n]->label : typeName(top[n]->type), top[n]->rssi);
    int lf = 1;
    for (const char* q = buf; *q; q++) if ((uint8_t)*q >= 0x80) { lf = 3; break; }
    int x = (int)sx, y = (int)sy - 3, w = txtW(buf, lf);
    for (int pass = 0; pass < 3; pass++)                  // labels must not overlap
      for (size_t m = 0; m < n; m++)
        if (abs(y - ly[m]) < 10 && abs(x - lx[m]) < (w + lw[m]) / 2 + 4) y = ly[m] - 10;
    y = max(y, UI_TOP + 10);
    lx[n] = x; ly[n] = y; lw[n] = w;
    spr.drawLine(x, y + 1, (int)sx, (int)sy - 1, UI_DIM());   // "leg" down to the roof
    spr.setTextColor(top[n]->alert ? C_ALERT() : C_HOT());
    txt(buf, x, y, lf);
  }

  snprintf(buf, sizeof(buf), "%d %s", (int)bl.size(), PL((int)bl.size(), "device", "devices", "устройство", "устройства", "устройств"));
  uiHeader(L("City", "Город"), buf);
  uiFooter(L("wheel: turn camera", "колесо: повернуть камеру"), L("side: back", "кнопка: назад"));
  present();
}
