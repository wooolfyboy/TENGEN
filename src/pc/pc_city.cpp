// ============================================================
//  pc_city.cpp — PC › City section: Zen Monolith Plaza of processes.
//  Height = CPU use, width = memory, white crown = active window,
//  45° crystal prism = >50% CPU. Wheel click cycles target spotlight.
// ============================================================
#include <algorithm>
#include "core/lang.h"
#include "pc/pc.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/timeutil.h"
#include "core/jsonutil.h"

#define CITY_MAX 24
#define CITY_GRID 6                          // 6x6 plots
struct Tower {
  char  name[20] = "";
  float cpu = 0, ram = 0;                    // sent by the laptop: %, MB
  float h = 0, w = 0;                        // animated height and width
  bool  fg = false, alive = false;
  int   slot = -1;
};
static Tower s_tw[CITY_MAX];
static uint32_t s_cityT = 0;
static int s_sel = -1;                       // spotlight (index in s_tw)

bool pcCityLive(uint32_t now) { return s_cityT && msSince(now, s_cityT) < 6000; }
void pcCityNext() {                          // click: next tower (by descending CPU)
  int order[CITY_MAX], n = 0;
  for (int i = 0; i < CITY_MAX; i++) if (s_tw[i].alive) order[n++] = i;
  if (!n) { s_sel = -1; return; }
  std::sort(order, order + n, [](int a, int b) { return s_tw[a].cpu > s_tw[b].cpu; });
  if (s_sel < 0) { s_sel = order[0]; return; }
  int k = n;
  for (int i = 0; i < n; i++) if (order[i] == s_sel) { k = i + 1; break; }
  s_sel = (k < n) ? order[k] : -1;
}

// "PP\tname,cpu*10,MB,active\t..."
void pcCityLine(char* s) {
  for (auto& t : s_tw) t.alive = false;
  char* save = nullptr;
  strtok_r(s, "\t", &save);
  for (char* tok = strtok_r(nullptr, "\t", &save); tok; tok = strtok_r(nullptr, "\t", &save)) {
    char* c1 = strchr(tok, ','); if (!c1) continue; *c1 = 0;
    char* c2 = strchr(c1 + 1, ','); if (!c2) continue; *c2 = 0;
    char* c3 = strchr(c2 + 1, ','); if (!c3) continue; *c3 = 0;
    int i = -1, free = -1;
    for (int k = 0; k < CITY_MAX; k++) {
      if (s_tw[k].name[0] && !strcmp(s_tw[k].name, tok)) i = k;
      if (free < 0 && !s_tw[k].name[0]) free = k;
    }
    if (i < 0) {                             // new program: free plot nearest the center
      if (free < 0) continue;
      i = free;
      Tower& t = s_tw[i];
      strncpy(t.name, tok, sizeof(t.name) - 1); t.name[sizeof(t.name) - 1] = 0;
      t.h = 0; t.w = 0;
      static const int8_t SPIRAL[36] = { 14, 15, 21, 20, 8, 9, 16, 22, 27, 26, 19, 13, 7, 2, 3, 10, 17, 23,
                                         28, 33, 32, 25, 18, 12, 6, 1, 4, 11, 29, 34, 31, 24, 0, 5, 30, 35 };
      for (int k = 0; k < 36; k++) {
        bool used = false;
        for (int j = 0; j < CITY_MAX; j++) if (j != i && s_tw[j].name[0] && s_tw[j].slot == SPIRAL[k]) used = true;
        if (!used) { t.slot = SPIRAL[k]; break; }
      }
    }
    Tower& t = s_tw[i];
    t.cpu = atoi(c1 + 1) / 10.0f; t.ram = atof(c2 + 1); t.fg = atoi(c3 + 1) != 0; t.alive = true;
  }
  s_cityT = millis();
}

// for the tablet: [[name,cpu,MB,active,slot],...]
void pcCityJson(String& o) {
  o += '[';
  bool first = true;
  for (auto& t : s_tw) {
    if (!t.alive) continue;
    if (!first) o += ',';
    first = false;
    o += '['; jsonStr(o, t.name); o += ','; o += String(t.cpu, 1); o += ','; o += (int)t.ram;
    o += ','; o += t.fg ? 1 : 0; o += ','; o += t.slot; o += ']';
  }
  o += ']';
}

static float s_yaw = 0.7f;

void drawPcCity(uint32_t now) {
  const int W = spr.width();
  g_fastFrame = true;
  spr.fillSprite(C_BG());
  s_yaw += 0.003f;
  const float cy_ = cosf(s_yaw), sy_ = sinf(s_yaw), pitch = 0.6f, cp = cosf(pitch), sp = sinf(pitch);
  const float F = 230, dist = 3.6f;
  const int cx = W / 2 - 44, cyS = 104;
  auto proj = [&](float x, float y, float z, float& sx, float& syy, float& d) {
    float xr = x * cy_ - z * sy_, zr = x * sy_ + z * cy_;
    float vy = y * cp + zr * sp, vz = -y * sp + zr * cp;
    d = dist + vz; sx = cx + xr * F / d; syy = cyS - vy * F / d;
  };

  const uint16_t line = UI_LINE(), acc = C_ACC(), hotC = C_HOT();
  // Zen circuit plaza grid + outer boundary accent
  const float half = 1.0f, step = 2 * half / CITY_GRID;
  for (int k = 0; k <= CITY_GRID; k++) {
    float a = -half + k * step, x0, y0, x1, y1, d;
    uint16_t gc = (k == 0 || k == CITY_GRID) ? scale565(acc, 65) : line;
    proj(a, 0, -half, x0, y0, d); proj(a, 0, half, x1, y1, d); spr.drawLine(x0, y0, x1, y1, gc);
    proj(-half, 0, a, x0, y0, d); proj(half, 0, a, x1, y1, d); spr.drawLine(x0, y0, x1, y1, gc);
  }
  // Central Tengen nexus marker
  float ox0, oy0, od0, ox1, oy1, od1;
  proj(0, 0, 0, ox0, oy0, od0); proj(0, 0.14f, 0, ox1, oy1, od1);
  spr.drawLine(ox0, oy0, ox1, oy1, scale565(acc, 70));
  drawMarker((int)ox0, (int)oy0, acc, false);

  struct D { int i; float depth; };
  D order[CITY_MAX]; int n = 0;
  float maxRam = 1;
  for (auto& t : s_tw) if (t.name[0] && t.ram > maxRam) maxRam = t.ram;
  for (int i = 0; i < CITY_MAX; i++) {
    Tower& t = s_tw[i];
    if (!t.name[0] || t.slot < 0) continue;
    float hT = t.alive ? 0.04f + 0.9f * powf(constrain(t.cpu / 100.0f, 0.0f, 1.0f), 0.6f) : 0;
    float wT = t.alive ? step * (0.25f + 0.33f * sqrtf(t.ram / maxRam)) : 0;
    t.h += (hT - t.h) * 0.12f; t.w += (wT - t.w) * 0.12f;
    if (!t.alive && t.h < 0.01f) { t.name[0] = 0; t.slot = -1; if (s_sel == i) s_sel = -1; continue; }
    float gx = -half + (t.slot % CITY_GRID + 0.5f) * step, gz = -half + (t.slot / CITY_GRID + 0.5f) * step;
    float sx, syy, d; proj(gx, 0, gz, sx, syy, d);
    order[n++] = { i, d };
  }
  std::sort(order, order + n, [](const D& a, const D& b) { return a.depth > b.depth; });

  for (int k = 0; k < n; k++) {
    Tower& t = s_tw[order[k].i];
    float gx = -half + (t.slot % CITY_GRID + 0.5f) * step, gz = -half + (t.slot / CITY_GRID + 0.5f) * step;
    bool hot = t.cpu > 50, sel = order[k].i == s_sel;
    float sb = t.w / 2, st = hot ? sb * 0.52f : (t.fg ? sb * 0.88f : sb * 0.72f);
    float bx[4], by[4], tx[4], ty[4], dd[4], d;
    for (int q = 0; q < 4; q++) {
      float ox0, oz0, ox1, oz1;
      if (hot) {
        const float RX[4] = { 0, 1.25f, 0, -1.25f }, RZ[4] = { -1.25f, 0, 1.25f, 0 };
        ox0 = RX[q] * sb; oz0 = RZ[q] * sb; ox1 = RX[q] * st; oz1 = RZ[q] * st;
      } else {
        const float CX[4] = { -1, 1, 1, -1 }, CZ[4] = { -1, -1, 1, 1 };
        ox0 = CX[q] * sb; oz0 = CZ[q] * sb; ox1 = CX[q] * st; oz1 = CZ[q] * st;
      }
      proj(gx + ox0, 0, gz + oz0, bx[q], by[q], dd[q]);
      proj(gx + ox1, t.h, gz + oz1, tx[q], ty[q], d);
    }
    if (sel || hot) {
      float lr = sb * 1.9f, lx[4], ly[4], ld;
      const float RX[4] = { 0, 1, 0, -1 }, RZ[4] = { -1, 0, 1, 0 };
      for (int q = 0; q < 4; q++) proj(gx + RX[q] * lr, 0, gz + RZ[q] * lr, lx[q], ly[q], ld);
      for (int q = 0; q < 4; q++) spr.drawLine(lx[q], ly[q], lx[(q + 1) & 3], ly[(q + 1) & 3], hot ? C_ALERT() : hotC);
    }
    uint16_t col = hot ? C_ALERT() : heatV(0.25f + 0.75f * constrain(t.cpu / 60.0f, 0.0f, 1.0f));
    int ord[4] = { 0, 1, 2, 3 };
    std::sort(ord, ord + 4, [&](int a, int b) { return dd[a] + dd[(a + 1) % 4] > dd[b] + dd[(b + 1) % 4]; });
    for (int o = 0; o < 4; o++) {
      int a = ord[o], b = (a + 1) % 4;
      uint16_t fc = scale565(col, (a % 2 == 0) ? 52 : 32);
      spr.fillTriangle(bx[a], by[a], bx[b], by[b], tx[b], ty[b], fc);
      spr.fillTriangle(bx[a], by[a], tx[b], ty[b], tx[a], ty[a], fc);
      if (o == 3 && t.h > 0.08f) {
        spr.drawLine((bx[a] + bx[b]) * 0.5f, (by[a] + by[b]) * 0.5f,
                     (tx[a] + tx[b]) * 0.5f, (ty[a] + ty[b]) * 0.5f, sel ? hotC : scale565(col, 90));
      }
    }
    uint16_t roof = t.fg ? hotC : col;
    spr.fillTriangle(tx[0], ty[0], tx[1], ty[1], tx[2], ty[2], roof);
    spr.fillTriangle(tx[0], ty[0], tx[2], ty[2], tx[3], ty[3], roof);
    for (int q = 0; q < 4; q++) spr.drawLine(tx[q], ty[q], tx[(q + 1) % 4], ty[(q + 1) % 4], sel ? hotC : scale565(hotC, 65));
    if (sel) {
      float px, py, pd, rx, ry, rd;
      proj(gx, t.h + 0.32f, gz, px, py, pd); proj(gx, t.h, gz, rx, ry, rd);
      spr.drawLine(px, py, rx, ry, hotC);
      spr.fillCircle((int)px, (int)py, 2, hot ? C_ALERT() : hotC);
    }
  }

  // Labels for the three busiest
  int top[3] = { -1, -1, -1 };
  for (int i = 0; i < CITY_MAX; i++) {
    if (!s_tw[i].alive) continue;
    for (int r = 0; r < 3; r++) if (top[r] < 0 || s_tw[i].cpu > s_tw[top[r]].cpu) { for (int q = 2; q > r; q--) top[q] = top[q - 1]; top[r] = i; break; }
  }
  spr.setTextDatum(BC_DATUM);
  int ly[3] = { -100, -100, -100 };
  for (int r = 0; r < 3; r++) {
    if (top[r] < 0) continue;
    Tower& t = s_tw[top[r]];
    float gx = -half + (t.slot % CITY_GRID + 0.5f) * step, gz = -half + (t.slot / CITY_GRID + 0.5f) * step;
    float sx, syy, d; proj(gx, t.h, gz, sx, syy, d);
    int y = (int)syy - 4;
    for (int q = 0; q < r; q++) if (abs(y - ly[q]) < 10) y = ly[q] - 10;
    y = max(y, UI_TOP + 10); ly[r] = y;
    spr.setTextColor(t.cpu > 50 ? C_ALERT() : hotC);
    txt(t.name, (int)sx, y, 1);
  }

  // Right telemetry panel (framed HUD readout)
  const int px = W - 108, pw = 100, py = UI_TOP + 2, ph = 118;
  hudPanel(px - 4, py, pw + 4, ph, 7, s_sel >= 0 ? acc : line, false, C_BG());
  char buf[32];
  spr.setTextDatum(TL_DATUM);
  int alive = 0; for (auto& t : s_tw) if (t.alive) alive++;
  if (s_sel >= 0 && s_tw[s_sel].alive) {
    Tower& t = s_tw[s_sel];
    spr.setTextColor(acc); txt(L("// LOCK", "// ФОКУС"), px, py + 4, 1);
    spr.setTextColor(hotC); char nm[20]; fitText(t.name, nm, sizeof(nm), pw - 4, 2); txt(nm, px, py + 16, 2);
    snprintf(buf, sizeof(buf), "%.1f%%", t.cpu);
    spr.setTextColor(t.cpu > 50 ? C_ALERT() : acc); txt(buf, px, py + 38, 4);
    spr.setTextColor(UI_DIM()); txt("CPU", px, py + 64, 1);
    if (t.ram >= 1024) snprintf(buf, sizeof(buf), L("%.1f GB RAM", "%.1f ГБ ОЗУ"), t.ram / 1024); else snprintf(buf, sizeof(buf), L("%d MB RAM", "%d МБ ОЗУ"), (int)t.ram);
    spr.setTextColor(hotC); txt(buf, px, py + 78, 1);
    if (t.fg) { spr.setTextColor(acc); txt(L("ACTIVE WIN", "АКТИВНО"), px, py + 94, 1); }
  } else {
    spr.setTextColor(UI_LABEL()); txt(L("MONOLITHS", "МОНОЛИТЫ"), px, py + 4, 1);
    snprintf(buf, sizeof(buf), "%d", alive);
    spr.setTextColor(hotC); txt(buf, px, py + 16, 4);
    spr.setTextColor(UI_DIM());
    txt(L("HEIGHT  CPU", "ВЫСОТА  ЦП"), px, py + 50, 1);
    txt(L("WIDTH   RAM", "ШИРИНА  ОЗУ"), px, py + 62, 1);
    txt(L("CROWN   ACTIVE", "ВЕРХ    ОКНО"), px, py + 74, 1);
    spr.setTextColor(C_ALERT()); txt(L("PRISM   >50%", "ПРИЗМА  >50%"), px, py + 86, 1);
  }
  if (!pcCityLive(now)) { spr.setTextColor(UI_DIM()); txt(L("WAITING...", "ЖДУ ПК..."), px, py + 104, 1); }
  snprintf(buf, sizeof(buf), L("%d%% total", "всего %d%%"), (int)lroundf(g_pc.cpu));
  uiHeader(L("Process city", "Город процессов"), buf, g_pc.cpu >= 90);
  uiFooter(L("wheel: screens  ·  click: lock", "колесо: экраны · нажать: фокус"), L("side: back", "кнопка: назад"));
  present();
}
