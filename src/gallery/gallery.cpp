// ============================================================
//  gallery.cpp — Gallery section
//
//  TESSERACT — 4D cube: rotates in the XW, YW, ZW
//              and XY planes, projected 4D → 3D → screen; the inner cube
//              turns inside out through the outer one. Trail = three past frames.
//  TORUS     — donut of ~1000 points; brightness = lighting and depth.
//  LIFE      — Conway's Game of Life with afterglow.
//  PENDULUM  — double pendulum: chaos and a glowing trail.
//  SKY/GLOBE — planetarium and satellites (gallery_sky.cpp).
//
//  Wheel = speed, click = back to the submenu.
// ============================================================
#include <algorithm>
#include "core/lang.h"
#include "core/timeutil.h"
#include "gallery/gallery.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/menu.h"
#include "core/psram.h"
#include "core/settings.h"
#include "gallery/astro.h"
#include "gallery/sat.h"
#include "gallery/weather.h"

enum { G_TESS = 0, G_TORUS, G_LIFE, G_PEND, G_SKY, G_GLOBE, G_WX, G_NUM };
static bool s_wxOpen = false;                   // Weather is open - its radios are on
static int   g_gal = 0;
static float s_speed = 1.0f;                // speed multiplier (wheel)
static float s_phase = 0;                   // animation "time", scaled by speed
static uint32_t s_lastMs = 0;

static void advance(uint32_t now) {
  uint32_t dt = s_lastMs ? min<uint32_t>(now - s_lastMs, 100) : 33;
  s_lastMs = now;
  s_phase += dt * 0.001f * s_speed;
}

static void speedLabel(char* buf, size_t n) { snprintf(buf, n, L("speed x%.1f", "скорость x%.1f"), s_speed); }

// ---------- TESSERACT ----------
#define TS_TRAIL 3
static int16_t s_trail[TS_TRAIL][16][2];    // screen points of past frames
static int     s_trailN = 0, s_trailHead = 0;

void drawTesseract(uint32_t now) {
  const int W = spr.width(), H = spr.height();
  g_fastFrame = true;
  advance(now);
  spr.fillSprite(C_BG());
  const float t = s_phase;
  const float a1 = t * 0.9f, a2 = t * 0.55f, a3 = t * 0.35f, a4 = t * 0.25f;
  const float c1 = cosf(a1), s1 = sinf(a1), c2 = cosf(a2), s2 = sinf(a2);
  const float c3 = cosf(a3), s3 = sinf(a3), c4 = cosf(a4), s4 = sinf(a4);
  const float ctilt = cosf(0.5f), stilt = sinf(0.5f);
  const int cx = W / 2, cy = (UI_TOP + UI_BOTTOM) / 2 + 2;

  float px[16], py[16], pw[16];
  for (int i = 0; i < 16; i++) {
    float x = (i & 1) ? 1 : -1, y = (i & 2) ? 1 : -1, z = (i & 4) ? 1 : -1, w = (i & 8) ? 1 : -1;
    float tx;
    tx = x * c1 - w * s1; w = x * s1 + w * c1; x = tx;   // XW
    tx = y * c2 - w * s2; w = y * s2 + w * c2; y = tx;   // YW
    tx = z * c3 - w * s3; w = z * s3 + w * c3; z = tx;   // ZW
    tx = x * c4 - y * s4; y = x * s4 + y * c4; x = tx;   // XY
    float k4 = 2.6f / (3.2f - w);                         // 4D → 3D (perspective by w)
    x *= k4; y *= k4; z *= k4;
    tx = y * ctilt - z * stilt; z = y * stilt + z * ctilt; y = tx;
    float k3 = 175.0f / (5.2f - z);                       // 3D → screen
    px[i] = cx + x * k3; py[i] = cy + y * k3; pw[i] = w;
  }
  // trail: older frames get dimmer
  for (int f = 0; f < s_trailN; f++) {
    int slot = (s_trailHead - 1 - f + TS_TRAIL * 2) % TS_TRAIL;
    uint16_t col = scale565(C_ACC(), 30 - f * 9);
    for (int i = 0; i < 16; i++)
      for (int b = 0; b < 4; b++) {
        int j = i ^ (1 << b);
        if (j > i) spr.drawLine(s_trail[slot][i][0], s_trail[slot][i][1], s_trail[slot][j][0], s_trail[slot][j][1], col);
      }
  }
  // edges join vertices that differ in one coordinate. Color = 4D "depth"
  for (int i = 0; i < 16; i++)
    for (int b = 0; b < 4; b++) {
      int j = i ^ (1 << b);
      if (j < i) continue;
      float w = (pw[i] + pw[j]) * 0.5f;                   // -1.4 .. 1.4
      uint16_t col = heatV(constrain(0.35f + w * 0.35f, 0.1f, 1.0f));
      spr.drawLine(px[i], py[i], px[j], py[j], col);
      spr.drawLine(px[i] + 1, py[i], px[j] + 1, py[j], scale565(col, 45));   // "thickness"
    }
  for (int i = 0; i < 16; i++) {
    int r = pw[i] > 0.3f ? 3 : 2;
    spr.fillCircle(px[i], py[i], r, pw[i] > 0 ? C_HOT() : C_ACC());
  }
  for (int i = 0; i < 16; i++) { s_trail[s_trailHead][i][0] = px[i]; s_trail[s_trailHead][i][1] = py[i]; }
  s_trailHead = (s_trailHead + 1) % TS_TRAIL;
  if (s_trailN < TS_TRAIL) s_trailN++;

  char buf[24]; speedLabel(buf, sizeof(buf));
  uiHeader(L("Tesseract", "Тессеракт"), buf);
  uiFooter(L("wheel: speed", "колесо: скорость"), L("side: back", "кнопка: назад"));
  present();
  (void)H;
}

// ---------- TORUS ----------
#define TO_U 56
#define TO_V 18
struct TPt { int16_t x, y; float z; float lum; };
static TPt*     s_pts = nullptr;                          // ~12 KB, in external memory (PSRAM)
static int16_t* s_order = nullptr;

void drawTorus(uint32_t now) {
  if (!s_pts) { s_pts = psAlloc<TPt>(TO_U * TO_V); s_order = psAlloc<int16_t>(TO_U * TO_V); }
  if (!s_pts || !s_order) return;
  const int W = spr.width();
  g_fastFrame = true;
  advance(now);
  spr.fillSprite(C_BG());
  const float A = s_phase * 0.8f, B = s_phase * 0.45f;
  const float cA = cosf(A), sA = sinf(A), cB = cosf(B), sB = sinf(B);
  const float R1 = 1.0f, R2 = 2.1f, K2 = 6.5f, K1 = 112.0f;
  const int cx = W / 2, cy = (UI_TOP + UI_BOTTOM) / 2 + 2;
  // light from above-front
  const float lx = 0, ly = 0.7071f, lz = -0.7071f;
  static float s_ct[TO_V], s_st[TO_V], s_cu[TO_U], s_su[TO_U], s_ci[TO_V], s_si[TO_V];
  static bool s_toLut = false;
  if (!s_toLut) {
    for (int i = 0; i < TO_V; i++) { float th = i * 2 * PI / TO_V; s_ct[i] = cosf(th); s_st[i] = sinf(th); s_ci[i] = cosf(i * 0.09f); s_si[i] = sinf(i * 0.09f); }
    for (int j = 0; j < TO_U; j++) { float ph = j * 2 * PI / TO_U; s_cu[j] = cosf(ph); s_su[j] = sinf(ph); }
    s_toLut = true;
  }
  int n = 0;
  for (int i = 0; i < TO_V; i++) {
    float ct = s_ct[i], st = s_st[i], ci = s_ci[i], si = s_si[i];
    float ox = R2 + R1 * ct, oy = R1 * st;                   // cross-section circle
    for (int j = 0; j < TO_U; j++) {
      float cp = s_cu[j] * ci - s_su[j] * si, sp = s_su[j] * ci + s_cu[j] * si;
      // rotate about Y (ph), then X (A), then Z (B)
      float x = ox * (cB * cp + sA * sB * sp) - oy * cA * sB;
      float y = ox * (sB * cp - sA * cB * sp) + oy * cA * cB;
      float z = K2 + cA * ox * sp + oy * sA;
      // normal
      float nx = ct * (cB * cp + sA * sB * sp) - st * cA * sB;
      float ny = ct * (sB * cp - sA * cB * sp) + st * cA * cB;
      float nz = cA * ct * sp + st * sA;
      float L = nx * lx + ny * ly + nz * lz;                 // -1..1
      TPt& p = s_pts[n++];
      p.x = cx + (int)(K1 * x / z); p.y = cy - (int)(K1 * y / z * 0.95f);
      p.z = z; p.lum = L;
    }
  }
  int16_t* order = s_order;
  for (int i = 0; i < n; i++) order[i] = i;
  std::sort(order, order + n, [](int16_t a, int16_t b) { return s_pts[a].z > s_pts[b].z; });   // far ones first
  for (int k = 0; k < n; k++) {
    const TPt& p = s_pts[order[k]];
    float depth = constrain((9.5f - p.z) / 7.0f, 0.15f, 1.0f);    // nearer = brighter
    float lum = p.lum > 0 ? p.lum : 0;
    float v = constrain(0.08f + 0.92f * lum * depth + 0.12f * depth, 0.0f, 1.0f);
    uint16_t col = heatV(v);
    if (v > 0.55f) spr.fillRect(p.x, p.y, 2, 2, col);
    else           spr.drawPixel(p.x, p.y, col);
  }
  char buf[24]; speedLabel(buf, sizeof(buf));
  uiHeader(L("Torus", "Тор"), buf);
  uiFooter(L("wheel: speed", "колесо: скорость"), L("side: back", "кнопка: назад"));
  present();
}

// ---------- LIFE — Conway's Game of Life ----------
// Wrap-around field. Newborn cells are white, old ones
// cool to teal and steel; dead cells leave a fading trail. If the field
// stalls or dies out, we drop in new shapes (gliders, R-pentomino).
#define LF_CELL 4
#define LF_W 76
#define LF_H 32
// fields live in external memory (PSRAM), allocated on first open of Life
static uint8_t  (*s_lf)[LF_W] = nullptr, (*s_lfN)[LF_W] = nullptr;   // age of a live cell (0 = dead)
static uint8_t  (*s_glow)[LF_W] = nullptr;                            // trail of dead cells
static const size_t LF_BYTES = LF_H * LF_W;
static uint32_t s_lfGen = 0, s_lfLast = 0, s_lfStill = 0, s_lfRng = 12345;
static int      s_lfPop = 0, s_lfPrevPop = -1;
static uint32_t s_lfHash[8];

static uint32_t lfRand() { s_lfRng ^= s_lfRng << 13; s_lfRng ^= s_lfRng >> 17; s_lfRng ^= s_lfRng << 5; return s_lfRng; }
static void lfSet(int x, int y) { s_lf[(y + LF_H) % LF_H][(x + LF_W) % LF_W] = 1; }

static void lfDrop() {                                   // drop a shape at a random spot
  int x = lfRand() % LF_W, y = lfRand() % LF_H;
  switch (lfRand() % 4) {
    case 0: lfSet(x + 1, y); lfSet(x + 2, y + 1); lfSet(x, y + 2); lfSet(x + 1, y + 2); lfSet(x + 2, y + 2); break;  // glider
    case 1: lfSet(x + 1, y); lfSet(x + 2, y); lfSet(x, y + 1); lfSet(x + 1, y + 1); lfSet(x + 1, y + 2); break;      // R-pentomino
    case 2: lfSet(x + 1, y); lfSet(x + 3, y + 1); lfSet(x, y + 2); lfSet(x + 1, y + 2);                              // "acorn"
            lfSet(x + 4, y + 2); lfSet(x + 5, y + 2); lfSet(x + 6, y + 2); break;
    default: for (int dy = 0; dy < 6; dy++) for (int dx = 0; dx < 6; dx++) if (lfRand() % 2) lfSet(x + dx, y + dy); break;   // blob
  }
}

static void lfSeed() {
  if (!s_lf) return;
  memset(s_lf, 0, LF_BYTES); memset(s_glow, 0, LF_BYTES);
  s_lfRng ^= millis() | 1;
  for (int y = 0; y < LF_H; y++) for (int x = 0; x < LF_W; x++) s_lf[y][x] = (lfRand() % 100) < 28;
  s_lfGen = 0; s_lfStill = 0; memset(s_lfHash, 0, sizeof(s_lfHash));
}

static void lfStep() {
  uint32_t hash = 2166136261u;
  int pop = 0;
  for (int y = 0; y < LF_H; y++) {
    int ym = (y + LF_H - 1) % LF_H, yp = (y + 1) % LF_H;
    for (int x = 0; x < LF_W; x++) {
      int xm = (x + LF_W - 1) % LF_W, xp = (x + 1) % LF_W;
      int n = (s_lf[ym][xm] > 0) + (s_lf[ym][x] > 0) + (s_lf[ym][xp] > 0) + (s_lf[y][xm] > 0) +
              (s_lf[y][xp] > 0) + (s_lf[yp][xm] > 0) + (s_lf[yp][x] > 0) + (s_lf[yp][xp] > 0);
      uint8_t a = s_lf[y][x], nv;
      if (a) nv = (n == 2 || n == 3) ? (uint8_t)min(a + 1, 255) : 0;
      else   nv = (n == 3) ? 1 : 0;
      if (a && !nv) s_glow[y][x] = 255;                  // died - leaves a trail
      else if (s_glow[y][x]) s_glow[y][x] = s_glow[y][x] > 28 ? s_glow[y][x] - 28 : 0;
      s_lfN[y][x] = nv;
      if (nv) { pop++; hash = (hash ^ (y * LF_W + x)) * 16777619u; }
    }
  }
  std::swap(s_lf, s_lfN);
  s_lfGen++;
  // stall: pattern repeats (blinkers, period up to 8) or population barely changes
  bool repeat = false;
  for (int i = 0; i < 8; i++) if (s_lfHash[i] == hash) repeat = true;
  s_lfHash[s_lfGen % 8] = hash;
  if (repeat || abs(pop - s_lfPrevPop) <= 1) s_lfStill++; else s_lfStill = 0;
  s_lfPrevPop = pop; s_lfPop = pop;
  if (pop < 12) lfSeed();                                 // died out - new world
  else if (s_lfStill > 40) { for (int i = 0; i < 4; i++) lfDrop(); s_lfStill = 0; }
}

void drawLife(uint32_t now) {
  if (!s_lf) {
    s_lf   = (uint8_t(*)[LF_W])psAlloc<uint8_t>(LF_BYTES);
    s_lfN  = (uint8_t(*)[LF_W])psAlloc<uint8_t>(LF_BYTES);
    s_glow = (uint8_t(*)[LF_W])psAlloc<uint8_t>(LF_BYTES);
    s_lfGen = 0; s_lfPop = 0;
  }
  if (!s_lf || !s_lfN || !s_glow) return;
  g_fastFrame = true;
  if (s_lfGen == 0 && s_lfPop == 0) lfSeed();
  uint32_t every = s_speed > 0 ? (uint32_t)(110 / s_speed) : 0;
  if (every && msSince(now, s_lfLast) >= every) { s_lfLast = now; lfStep(); }
  spr.fillSprite(C_BG());
  const int x0 = (spr.width() - LF_W * LF_CELL) / 2, y0 = UI_TOP + 1;
  for (int y = 0; y < LF_H; y++)
    for (int x = 0; x < LF_W; x++) {
      uint8_t a = s_lf[y][x];
      int px = x0 + x * LF_CELL, py = y0 + y * LF_CELL;
      if (a) {
        uint16_t c = a == 1 ? C_HOT() : heatV(max(0.3f, 0.9f - a * 0.03f));   // newborn is white, ages to teal
        spr.fillRect(px, py, LF_CELL - 1, LF_CELL - 1, c);
      } else if (s_glow[y][x]) {
        spr.fillRect(px, py, LF_CELL - 1, LF_CELL - 1, scale565(C_ACC(), s_glow[y][x] * 30 / 255));
      }
    }
  char buf[40];
  snprintf(buf, sizeof(buf), L("gen %lu  ·  cells %d  ·  x%.1f", "пок. %lu  ·  клеток %d  ·  x%.1f"), (unsigned long)s_lfGen, s_lfPop, s_speed);
  uiHeader(L("Life", "Жизнь"), buf);
  uiFooter(L("wheel: speed  ·  click: new world", "колесо: скорость · нажать: новый мир"), L("side: back", "кнопка: назад"));
  present();
}

// ---------- PENDULUM — double pendulum ----------
// Two coupled pendulums, Lagrange equations, 4th-order Runge-Kutta step.
// The lower bob leaves a glowing trail: brighter and whiter where faster.
#define PD_TRAIL 420
static float   s_th1, s_th2, s_w1, s_w2;                // angles and angular velocities
static int16_t s_tx[PD_TRAIL], s_ty[PD_TRAIL];
static uint8_t s_tv[PD_TRAIL];                           // speed at the trail point (0..255)
static int     s_tHead = 0, s_tN = 0;
static bool    s_pdInit = false;

static void pdDeriv(const float* y, float* d) {          // y = th1, th2, w1, w2 (m1=m2, l1=l2)
  const float g = 9.81f, l = 1.0f;
  float t1 = y[0], t2 = y[1], w1 = y[2], w2 = y[3], dt = t1 - t2;
  float den = 3.0f - cosf(2 * dt);                       // 2m1+m2 - m2 cos(2Δ) for m1=m2=1
  d[0] = w1; d[1] = w2;
  d[2] = (-g * 3 * sinf(t1) - g * sinf(t1 - 2 * t2) - 2 * sinf(dt) * (w2 * w2 * l + w1 * w1 * l * cosf(dt))) / (l * den);
  d[3] = (2 * sinf(dt) * (w1 * w1 * l * 2 + g * 2 * cosf(t1) + w2 * w2 * l * cosf(dt))) / (l * den);
}
static void pdStep(float h) {
  float y[4] = { s_th1, s_th2, s_w1, s_w2 }, k1[4], k2[4], k3[4], k4[4], t[4];
  pdDeriv(y, k1);
  for (int i = 0; i < 4; i++) t[i] = y[i] + k1[i] * h / 2;
  pdDeriv(t, k2);
  for (int i = 0; i < 4; i++) t[i] = y[i] + k2[i] * h / 2;
  pdDeriv(t, k3);
  for (int i = 0; i < 4; i++) t[i] = y[i] + k3[i] * h;
  pdDeriv(t, k4);
  for (int i = 0; i < 4; i++) y[i] += h / 6 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
  s_th1 = y[0]; s_th2 = y[1]; s_w1 = y[2]; s_w2 = y[3];
}
static void pdReset() {
  uint32_t r = esp_random();
  s_th1 = 2.0f + (r % 1000) / 1000.0f * 1.0f;            // high: chaos
  s_th2 = 1.5f + ((r >> 10) % 1000) / 1000.0f * 1.6f;
  s_w1 = s_w2 = 0; s_tN = 0; s_tHead = 0; s_pdInit = true;
}

void drawPendulum(uint32_t now) {
  g_fastFrame = true;
  if (!s_pdInit) pdReset();
  uint32_t prev = s_lastMs;
  advance(now);
  float dtReal = prev ? min<uint32_t>(now - prev, 100) * 0.001f * s_speed : 0.033f;
  const int W = spr.width();
  const int cx = W / 2, cy = 84;
  const float L = 33;
  int steps = 6;
  for (int k = 0; k < steps; k++) {
    pdStep(dtReal / steps);
    // a trail point on every substep keeps the line smooth
    if (k % 2 == 1) {
      float x1 = cx + L * sinf(s_th1), y1 = cy + L * cosf(s_th1);
      int x2 = (int)(x1 + L * sinf(s_th2)), y2 = (int)(y1 + L * cosf(s_th2));
      float v = fabsf(s_w2) + fabsf(s_w1) * 0.5f;
      s_tx[s_tHead] = x2; s_ty[s_tHead] = y2; s_tv[s_tHead] = (uint8_t)min(255.0f, v * 22);
      s_tHead = (s_tHead + 1) % PD_TRAIL; if (s_tN < PD_TRAIL) s_tN++;
    }
  }
  if (!isfinite(s_th1) || !isfinite(s_th2)) pdReset();   // just in case
  spr.fillSprite(C_BG());
  // trail: older points dimmer; color = speed
  for (int i = 1; i < s_tN; i++) {
    int a = (s_tHead - s_tN + i - 1 + PD_TRAIL) % PD_TRAIL, b = (a + 1) % PD_TRAIL;
    int age = 100 * i / s_tN;                            // 0 = oldest
    uint16_t c = scale565(heatV(0.35f + 0.65f * s_tv[b] / 255.0f), 12 + age * 88 / 100);
    spr.drawLine(s_tx[a], s_ty[a], s_tx[b], s_ty[b], c);
  }
  // pendulum
  float x1 = cx + L * sinf(s_th1), y1 = cy + L * cosf(s_th1);
  float x2 = x1 + L * sinf(s_th2), y2 = y1 + L * cosf(s_th2);
  spr.drawCircle(cx, cy, (int)(2 * L), UI_LINE());
  spr.drawLine(cx, cy, x1, y1, UI_LABEL());
  spr.drawLine(x1, y1, x2, y2, UI_LABEL());
  spr.fillCircle(cx, cy, 2, UI_DIM());
  spr.fillCircle(x1, y1, 4, C_ACC());
  spr.fillCircle(x2, y2, 5, C_HOT());
  char buf[24]; speedLabel(buf, sizeof(buf));
  uiHeader(L("Pendulum", "Маятник"), buf);
  uiFooter(L("wheel: speed  ·  click: new swing", "колесо: скорость · нажать: новый"), L("side: back", "кнопка: назад"));
  present();
}

// ---------- section ----------
static void galSubtitle(char* out, size_t n) { snprintf(out, n, L("%d scenes", "%d сцен"), G_NUM); }
static void galEnter() { s_lastMs = 0; }
static void galLeave() { if (s_wxOpen) { wxStop(); s_wxOpen = false; } }
static void galInfo(int i, SubItem& it) {
  const char* const names[G_NUM] = { L("Tesseract", "Тессеракт"), L("Torus", "Тор"), L("Life", "Жизнь"), L("Pendulum", "Маятник"), L("Sky", "Небо"), L("Globe", "Глобус"), L("Weather", "Погода") };
  const char* const descs[G_NUM] = { L("4D hypercube", "4D-гиперкуб"), L("donut of points", "тор из точек"), L("game of life", "игра «Жизнь»"), L("double pendulum chaos", "двойной маятник"), L("stars over you now", "звёзды сейчас"), L("ISS and satellites", "МКС и спутники"), L("radio weather", "радиопогода") };
  it.name = names[i]; it.desc = descs[i]; it.value[0] = 0;
  if (i == G_TESS)       strcpy(it.value, "4D");
  else if (i == G_TORUS) strcpy(it.value, "3D");
  else if (s_lfGen && i == G_LIFE) snprintf(it.value, sizeof(it.value), L("gen %lu", "пок. %lu"), (unsigned long)s_lfGen);
  else if (i == G_SKY)   snprintf(it.value, sizeof(it.value), "%s", LT(curCity().name));
  else if (i == G_GLOBE) snprintf(it.value, sizeof(it.value), L("%d sats", "%d спутн."), g_nSats);
  else if (i == G_WX && g_wx.ok) snprintf(it.value, sizeof(it.value), "%s", g_wx.cond);
}
static int  galNow() { return g_gal; }
static void galOpen(int m) {
  g_gal = m; s_lastMs = 0; s_trailN = 0;
  if (m == G_WX) { wxStart(); s_wxOpen = true; }      // radio weather: turn on its sources (BLE, CC1101 433, microphone)
}
static void galClose() { if (s_wxOpen) { wxStop(); s_wxOpen = false; } }
static void galTick(uint32_t now) { if (s_wxOpen) { wxRadiosTick(now); wxUpdate(now); } }
static void galClick() {                                // Life: new world; pendulum: new run
  if (g_gal == G_LIFE) lfSeed(); else if (g_gal == G_PEND) pdReset();   // the other scenes have no click action
  else if (g_gal == G_SKY) skyClick(); else if (g_gal == G_GLOBE) globeClick();
}
static void galRotate(int dir) {
  if (g_gal == G_SKY) { skyTurn(dir); return; }              // sky and Earth: the wheel turns the view
  if (g_gal == G_GLOBE) { globeTurn(dir); return; }
  if (g_gal == G_WX) { wxTurn(dir); return; }
  s_speed = constrain(s_speed + dir * 0.25f, 0.0f, 4.0f);
}
static void galDraw(uint32_t now) {
  if (g_gal == G_TESS) drawTesseract(now); else if (g_gal == G_TORUS) drawTorus(now);
  else if (g_gal == G_LIFE) drawLife(now); else if (g_gal == G_PEND) drawPendulum(now);
  else if (g_gal == G_SKY) drawSky(now); else if (g_gal == G_GLOBE) drawGlobe(now); else drawWeather(now);
}

const Section gallerySection = {
  LP("Gallery", "Галерея"), 150, 120, 255, iconCube, galSubtitle,
  galEnter, galLeave,
  G_NUM, galInfo, galNow, galOpen, galClose,
  galClick, galRotate, nullptr, galTick, galDraw,
};
