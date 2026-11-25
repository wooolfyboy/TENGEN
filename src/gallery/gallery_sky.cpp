// ============================================================
//  gallery_sky.cpp — Gallery › Sky (planetarium) and Globe (satellites)
//
//  SKY   — the sky over the city right now: a dome (zenith at the center,
//          horizon at the rim, north up, east left, like a
//          star chart), ~1600 stars, constellation lines, Sun, Moon
//          with phase, planets. Wheel turns the dome, click toggles labels.
//  GLOBE — Earth with continents, day and night, ISS and other stations on
//          their orbits, ground track, visibility zone and the next
//          pass over your city. Wheel turns the Earth, click =
//          follow the ISS / home / the next satellite.
// ============================================================
#include "gallery/gallery.h"
#include "core/lang.h"
#include "gallery/astro.h"
#include "gallery/astrodata.h"
#include "gallery/sat.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/settings.h"
#include "core/timeutil.h"

static const float D2R = PI / 180.0f;

static void hhmm(double unixUtc, char* out, size_t n, bool withSec = false) {
  long t = (long)(unixUtc + astroTzOffset());
  long d = ((t % 86400) + 86400) % 86400;
  if (withSec) snprintf(out, n, "%02ld:%02ld:%02ld", d / 3600, d / 60 % 60, d % 60);
  else         snprintf(out, n, "%02ld:%02ld", d / 3600, d / 60 % 60);
}

// ============================================================
//  SKY
// ============================================================
static float s_skyRot = 0, s_skyRotT = 0;     // dome rotation (wheel), smoothed
static int   s_skyLabels = 0;                 // 0 constellations+planets, 1 bright stars, 2 no labels

void skyTurn(int dir) { s_skyRotT += dir * 15; }
void skyClick() { s_skyLabels = (s_skyLabels + 1) % 3; }

struct DomeView { int cx, cy, R; float rot; };

// dome labels must not overlap: remember occupied spots
static int s_lblN = 0;
static int16_t s_lbl[64][4];
static bool labelFree(int x, int y, int w, int h) {
  for (int i = 0; i < s_lblN; i++)
    if (x < s_lbl[i][0] + s_lbl[i][2] && s_lbl[i][0] < x + w && y < s_lbl[i][1] + s_lbl[i][3] && s_lbl[i][1] < y + h) return false;
  if (s_lblN < 64) { s_lbl[s_lblN][0] = x; s_lbl[s_lblN][1] = y; s_lbl[s_lblN][2] = w; s_lbl[s_lblN][3] = h; s_lblN++; }
  return true;
}
static void label(const char* t, int x, int y) {         // label centered at (x, y) if there is room
  int w = txtW(t, 1);
  if (labelFree(x - w / 2 - 1, y - 4, w + 2, 9)) txt(t, x, y, 1);
}
// stereographic projection: zenith at center, horizon at the rim; north up, east left
static bool domeXY(const DomeView& v, float alt, float az, int& x, int& y) {
  if (alt < 0) return false;
  float r = v.R * tanf((90 - alt) * D2R / 2);                 // tan(45°)=1 at the horizon
  float a = (az - v.rot) * D2R;
  x = v.cx - (int)(r * sinf(a));
  y = v.cy - (int)(r * cosf(a));
  return true;
}

// Direct stereographic projection from RA/Dec -> screen (eliminates asinf, atan2f, tanf, and 2 sin/cos per star)
static inline bool domeRaDec(const SkyFrame& sf, const DomeView& v, float cr, float sr, float raDeg, float decDeg, int& x, int& y, float minSinAlt = 0.0f) {
  float H = (sf.lst - raDeg) * D2R, d = decDeg * D2R;
  float sd = sinf(d), cd = cosf(d), sH = sinf(H), cH = cosf(H);
  float sa = sf.sinLat * sd + sf.cosLat * cd * cH;
  if (sa < minSinAlt) return false;
  float east = -cd * sH;
  float north = sd * sf.cosLat - cd * sf.sinLat * cH;
  float k = v.R / (1.0f + sa);
  x = v.cx - (int)(k * (east * cr - north * sr));
  y = v.cy - (int)(k * (north * cr + east * sr));
  return true;
}

void drawSky(uint32_t now) {
  const int W = spr.width();
  g_fastFrame = true;
  s_skyRot += (s_skyRotT - s_skyRot) * 0.25f;
  spr.fillSprite(C_BG());
  const City& c = curCity();
  bool timeOk = astroTimeKnown();
  double t = astroUnixNow();
  double jd = julianDate(t);
  const SkyFrame sf = skyFrame(jd, c.lat, c.lon);       // once per frame, so the stars are then cheap to compute

  RaDec sun = sunRaDec(jd);
  AltAz sunA = toAltAz(sun.ra, sun.dec, jd, c.lat, c.lon);
  bool day = sunA.alt > -6, night = sunA.alt < -12;

  DomeView v = { 86, 88, 58, s_skyRot };
  const float cr = cosf(v.rot * D2R), sr = sinf(v.rot * D2R);
  s_lblN = 0;
  spr.fillCircle(v.cx, v.cy, v.R, day ? TC(8, 30, 44) : night ? TC(3, 10, 16) : TC(5, 20, 30));
  static float s_c72[72], s_s72[72];
  static bool s_lut72 = false;
  if (!s_lut72) {
    for (int k = 0; k < 72; k++) { float a = k * 5 * D2R; s_c72[k] = cosf(a); s_s72[k] = sinf(a); }
    s_lut72 = true;
  }
  for (int alt = 30; alt < 90; alt += 30) {                    // altitude circles at 30° and 60°
    int r = (int)(v.R * tanf((90 - alt) * D2R / 2));
    for (int k = 0; k < 72; k++) spr.drawPixel(v.cx + (int)(r * s_c72[k]), v.cy + (int)(r * s_s72[k]), UI_LINE());
  }
  spr.drawCircle(v.cx, v.cy, v.R, C_ACC());

  // constellation lines
  uint16_t clCol = scale565(C_ACC(), day ? 25 : 45);
  for (int k = 0; k < N_CL; k++) {
    int px = 0, py = 0; bool have = false;
    for (int i = CL_START[k]; i < CL_START[k + 1]; i++) {
      int x, y;
      if (domeRaDec(sf, v, cr, sr, CL_PTS[i][0] * 0.01f, (int16_t)CL_PTS[i][1] * 0.01f, x, y)) {
        if (have) spr.drawLine(px, py, x, y, clCol);
        px = x; py = y; have = true;
      } else have = false;
    }
  }
  // stars: by day only the brightest show (and dimly)
  float magLimit = day ? 1.5f : night ? 5.0f : 3.0f;
  const uint16_t cHot = C_HOT(), cM1 = heatV(0.9f), cM2 = heatV(0.75f);
  for (int i = 0; i < N_STARS; i++) {
    float mag = STARS[i].mag * 0.1f - 2.0f;
    if (mag > magLimit) break;                                   // sorted by brightness
    int x, y;
    if (!domeRaDec(sf, v, cr, sr, STARS[i].ra * 0.01f, STARS[i].dec * 0.01f, x, y)) continue;
    if (mag < 0.5f)      spr.fillCircle(x, y, 2, cHot);
    else if (mag < 1.5f) { spr.fillRect(x - 1, y - 1, 3, 3, cM1); spr.drawPixel(x, y, cHot); }
    else if (mag < 2.5f) spr.fillRect(x, y, 2, 2, cM2);
    else                 spr.drawPixel(x, y, heatV(max(0.15f, 0.65f - (mag - 2.5f) * 0.2f)));
  }
  // planets
  int visPl[PL_NUM], nVis = 0; float visAlt[PL_NUM];
  for (int p = 0; p < PL_NUM; p++) {
    RaDec r = planetRaDec(p, jd);
    AltAz a = toAltAz(r.ra, r.dec, jd, c.lat, c.lon);
    int x, y;
    if (!domeXY(v, a.alt, a.az, x, y)) continue;
    visPl[nVis] = p; visAlt[nVis++] = a.alt;
    spr.fillCircle(x, y, 2, C_HOT());
    spr.drawCircle(x, y, 4, C_ACC());
    if (s_skyLabels == 0) {
      const char* const EN[PL_NUM] = { L("MER", "МЕР"), L("VEN", "ВЕН"), L("MAR", "МАР"), L("JUP", "ЮП"), L("SAT", "САТ") };
      spr.setTextColor(C_HOT());
      label(EN[p], x, y - 9);
    }
  }
  // labels: constellations (Latin abbreviations) or bright stars
  spr.setTextDatum(MC_DATUM);
  if (s_skyLabels == 0) {
    spr.setTextColor(scale565(C_ACC(), 70));
    const float sin12 = sinf(12.0f * D2R);
    for (int i = 0; i < N_CON_NAMES; i++) {
      int x, y;
      if (domeRaDec(sf, v, cr, sr, CON_NAMES[i].ra * 0.01f, CON_NAMES[i].dec * 0.01f, x, y, sin12)) label(CON_NAMES[i].ab, x, y);
    }
  } else if (s_skyLabels == 1) {
    spr.setTextColor(UI_LABEL());
    const float sin5 = sinf(5.0f * D2R);
    for (int i = 0; i < N_STAR_NAMES; i++) {
      const AStar& s = STARS[STAR_NAMES[i].star];
      int x, y;
      if (domeRaDec(sf, v, cr, sr, s.ra * 0.01f, s.dec * 0.01f, x, y, sin5)) label(STAR_NAMES[i].name, x, y - 7);
    }
  }
  // Moon with phase, and Sun
  float phase; RaDec mo = moonRaDec(jd, &phase);
  AltAz moA = toAltAz(mo.ra, mo.dec, jd, c.lat, c.lon);
  int x, y;
  if (domeXY(v, moA.alt, moA.az, x, y)) {
    spr.fillCircle(x, y, 5, scale565(C_HOT(), 25));
    int lit = (int)(phase * 11) - 5;                            // lit part as a strip
    for (int dx = -5; dx <= 5; dx++) if (dx <= lit) {
      int h = (int)sqrtf(25 - dx * dx);
      spr.drawFastVLine(x + dx, y - h, 2 * h + 1, C_HOT());
    }
  }
  if (domeXY(v, sunA.alt, sunA.az, x, y)) {
    spr.fillCircle(x, y, 5, C_HOT());
    for (int k = 0; k < 8; k++) { float a = k * 45 * D2R; spr.drawLine(x + cosf(a) * 7, y + sinf(a) * 7, x + cosf(a) * 10, y + sinf(a) * 10, C_HOT()); }
  }
  // compass sides (rotate with the dome)
  const char* const SIDES[4] = { L("N", "С"), L("E", "В"), L("S", "Ю"), L("W", "З") };
  spr.setTextColor(C_ACC());
  for (int k = 0; k < 4; k++) {
    float a = (k * 90 - v.rot) * D2R;
    txt(SIDES[k], v.cx - (int)((v.R + 7) * sinf(a)), v.cy - (int)((v.R + 7) * cosf(a)), 1);
  }

  // ---- right: time, Sun, Moon, planets ----
  const int px = 168;
  char buf[40];
  spr.setTextDatum(TL_DATUM);
  if (timeOk) { hhmm(t, buf, sizeof(buf)); spr.setTextColor(C_HOT()); txt(buf, px, UI_TOP + 2, 4); }
  else { spr.setTextColor(C_ALERT()); txt(L("TIME?", "ВРЕМЯ?"), px, UI_TOP + 2, 4); }
  spr.setTextColor(UI_LABEL());
  txt(timeOk ? L("LOCAL TIME", "МЕСТНОЕ ВРЕМЯ") : L("CONNECT TO AN APP ONCE", "ПОДКЛЮЧИТЕ ПРИЛОЖЕНИЕ"), px, UI_TOP + 28, 1);
  snprintf(buf, sizeof(buf), L("SUN %+d", "СОЛНЦЕ %+d"), (int)lroundf(sunA.alt));
  spr.setTextColor(C_HOT()); txt(buf, px, UI_TOP + 44, 1);
  spr.setTextColor(UI_DIM()); txt(day ? (sunA.alt > 0 ? L("DAY", "ДЕНЬ") : L("TWILIGHT", "СУМЕРКИ")) : night ? L("NIGHT", "НОЧЬ") : L("TWILIGHT", "СУМЕРКИ"), px + 64, UI_TOP + 44, 1);
  snprintf(buf, sizeof(buf), L("MOON %d%%  %+d", "ЛУНА %d%%  %+d"), (int)lroundf(phase * 100), (int)lroundf(moA.alt));
  spr.setTextColor(C_HOT()); txt(buf, px, UI_TOP + 56, 1);
  int yy = UI_TOP + 70;
  spr.setTextColor(UI_LABEL());
  txt(nVis ? L("PLANETS UP, DEG", "ПЛАНЕТЫ НАД ГОРИЗОНТОМ") : L("NO PLANETS UP", "ПЛАНЕТ НЕ ВИДНО"), px, yy, 1);
  for (int i = 0; i < nVis && i < 3; i++) {                // Russian text, font with Cyrillic
    snprintf(buf, sizeof(buf), "%s  %d", LT(PLANET_RU[visPl[i]]), (int)lroundf(visAlt[i]));
    spr.setTextColor(C_HOT());
    txt(buf, px, yy + 11 + i * 15, 3);
  }
  if (nVis > 3) { snprintf(buf, sizeof(buf), "+%d", nVis - 3); spr.setTextColor(UI_DIM()); spr.setTextDatum(TR_DATUM); txt(buf, spr.width() - UI_M - 4, yy, 1); spr.setTextDatum(TL_DATUM); }
  snprintf(buf, sizeof(buf), "%s", LT(c.name));
  uiHeader(L("Sky", "Небо"), buf);
  const char* const LBL[3] = { L("wheel: turn  ·  click: stars", "колесо: поворот · нажать: звёзды"), L("wheel: turn  ·  click: no labels", "колесо: поворот · нажать: чисто"), L("wheel: turn  ·  click: constellations", "колесо: поворот · нажать: созвездия") };
  uiFooter(LBL[s_skyLabels], L("side: back", "кнопка: назад"));
  present();
  (void)now; (void)W;
}

// ============================================================
//  GLOBE
// ============================================================
static float s_gLon = 30, s_gLat = 25, s_gLonOff = 0, s_gLonOffT = 0;
static int   s_gFollow = 0;                    // 0 first satellite (ISS), 1 home, 2.. other satellites
static Pass  s_pass;
static double s_passAt = 0;

void globeTurn(int dir) { s_gLonOffT += dir * 20; }
void globeClick() {
  s_gFollow = (s_gFollow + 1) % (g_nSats > 1 ? g_nSats + 1 : 2);
  s_gLonOffT = s_gLonOff = 0;
}

struct GlobeView { int cx, cy, R; float lat0, lon0; float sl, cl; };
static bool globeXY(const GlobeView& g, float lat, float lon, int& x, int& y) {
  float la = lat * D2R, dl = (lon - g.lon0) * D2R;
  float cosc = g.sl * sinf(la) + g.cl * cosf(la) * cosf(dl);
  if (cosc < 0) return false;                                  // far side of the Earth
  x = g.cx + (int)(g.R * cosf(la) * sinf(dl));
  y = g.cy - (int)(g.R * (g.cl * sinf(la) - g.sl * cosf(la) * cosf(dl)));
  return true;
}

void drawGlobe(uint32_t now) {
  g_fastFrame = true;
  spr.fillSprite(C_BG());
  const City& c = curCity();
  bool timeOk = astroTimeKnown();
  double t = astroUnixNow();
  double jd = julianDate(t);

  // what we follow
  SatPos sp[SAT_MAX]; bool spOk[SAT_MAX] = {};
  for (int i = 0; i < g_nSats; i++) spOk[i] = timeOk && satPos(g_sats[i], t, sp[i]);
  int satIdx = s_gFollow == 0 ? 0 : s_gFollow >= 2 ? s_gFollow - 1 : -1;
  float tLat = c.lat, tLon = c.lon;
  if (satIdx >= 0 && satIdx < g_nSats && spOk[satIdx]) { tLat = sp[satIdx].lat; tLon = sp[satIdx].lon; }
  s_gLonOff += (s_gLonOffT - s_gLonOff) * 0.2f;
  float wantLon = tLon + s_gLonOff, wantLat = constrain(tLat * 0.6f + 10, -50.0f, 60.0f);
  float dLon = fmodf(wantLon - s_gLon + 540, 360) - 180;
  s_gLon += dLon * 0.08f; s_gLat += (wantLat - s_gLat) * 0.08f;

  GlobeView g = { 86, 90, 60, s_gLat, s_gLon, sinf(s_gLat * D2R), cosf(s_gLat * D2R) };
  // day and night: for each disc point, is it lit by the Sun
  RaDec sun = sunRaDec(jd);
  float sLon = sun.ra - (float)gmstDeg(jd), sLat = sun.dec;      // subsolar point
  float sx = cosf(sLat * D2R) * cosf(sLon * D2R), sy = cosf(sLat * D2R) * sinf(sLon * D2R), sz = sinf(sLat * D2R);
  float cl0 = cosf(g.lon0 * D2R), sl0 = sinf(g.lon0 * D2R);
  const uint16_t dayC = TC(10, 38, 54), nightC = TC(3, 11, 17), twiC = TC(6, 22, 32);
  if (!timeOk) {
    spr.fillCircle(g.cx, g.cy, g.R, twiC);
  } else {
    // Rotate Sun vector into camera space once: d*R = xx*csx - yy*csy + sqrt(R² - xx² - yy²)*csz
    float csx = -sx * sl0 + sy * cl0;
    float csy = (-sx * cl0 - sy * sl0) * g.sl + sz * g.cl;
    float csz = ( sx * cl0 + sy * sl0) * g.cl + sz * g.sl;
    float thr = 0.05f * g.R;
    int R2 = g.R * g.R;
    for (int yy = -g.R; yy <= g.R; yy++) {
      int r2y = R2 - yy * yy;
      int half = (int)sqrtf((float)r2y);
      float base = -yy * csy;
      int runStart = -half;
      uint16_t runCol = 0;
      for (int xx = -half; xx <= half; xx++) {
        float dr = base + xx * csx + sqrtf((float)(r2y - xx * xx)) * csz;
        uint16_t c0 = dr > thr ? dayC : dr > -thr ? twiC : nightC;
        if (xx == -half) runCol = c0;
        else if (c0 != runCol) {
          spr.drawFastHLine(g.cx + runStart, g.cy + yy, xx - runStart, runCol);
          runStart = xx; runCol = c0;
        }
      }
      spr.drawFastHLine(g.cx + runStart, g.cy + yy, half - runStart + 1, runCol);
    }
  }
  spr.drawCircle(g.cx, g.cy, g.R, C_ACC());
  // 30° grid
  for (int la = -60; la <= 60; la += 30)
    for (int lo = -180; lo < 180; lo += 4) { int x, y; if (globeXY(g, la, lo, x, y)) spr.drawPixel(x, y, UI_LINE()); }
  for (int lo = -180; lo < 180; lo += 30)
    for (int la = -88; la <= 88; la += 4) { int x, y; if (globeXY(g, la, lo, x, y)) spr.drawPixel(x, y, UI_LINE()); }
  // continents
  for (int k = 0; k < N_LAND; k++) {
    int px = 0, py = 0; bool have = false;
    for (int i = LAND_START[k]; i < LAND_START[k + 1]; i++) {
      int x, y;
      if (globeXY(g, LAND_PTS[i][1] / 100.0f, LAND_PTS[i][0] / 100.0f, x, y)) { if (have) spr.drawLine(px, py, x, y, C_ACC()); px = x; py = y; have = true; }
      else have = false;
    }
  }
  // home
  int hx, hy;
  if (globeXY(g, c.lat, c.lon, hx, hy)) {
    int pr = 3 + (int)((now / 150) % 4);
    spr.drawCircle(hx, hy, pr, scale565(C_HOT(), 70));
    spr.fillCircle(hx, hy, 2, C_HOT());
  }
  // satellites: track (−30 … +90 min), ISS visibility zone, markers
  for (int i = 0; i < g_nSats; i++) {
    if (!spOk[i]) continue;
    bool main = (i == (satIdx >= 0 ? satIdx : 0));
    // track is recomputed every 20 s (61 double-precision orbit solves are too costly per frame);
    // it barely moves in 20 s, and the current satellite point is computed every frame
    static struct { double t = -1e12, epoch = 0; int n = 0; float la[61], lo[61]; } trk[SAT_MAX];
    auto& tc = trk[i];
    if (fabs(t - tc.t) > 20 || tc.epoch != g_sats[i].epochUnix) {
      tc.t = t; tc.epoch = g_sats[i].epochUnix; tc.n = 0;
      for (int m = -30; m <= 90; m += 2) {
        SatPos q; if (!satPos(g_sats[i], t + m * 60, q)) break;
        tc.la[tc.n] = q.lat; tc.lo[tc.n] = q.lon; tc.n++;
      }
    }
    int px = 0, py = 0; bool have = false;
    for (int k = 0; k < tc.n; k++) {
      const int m = -30 + k * 2;
      int x, y;
      if (globeXY(g, tc.la[k], tc.lo[k], x, y)) {
        if (have) spr.drawLine(px, py, x, y, m < 0 ? scale565(C_ACC(), 35) : (main ? C_ACC() : scale565(C_ACC(), 55)));
        px = x; py = y; have = true;
      } else have = false;
    }
    if (main) {                                                  // where the satellite is seen above 10°
      float el = 10 * D2R, h = sp[i].altKm, lam = acosf(6378.0f * cosf(el) / (6378.0f + h)) - el;
      float la0 = sp[i].lat * D2R, lo0 = sp[i].lon * D2R;
      int fx = 0, fy = 0; bool fh = false;
      for (int k = 0; k <= 48; k++) {
        float b = k * 7.5f * D2R;
        float la = asinf(sinf(la0) * cosf(lam) + cosf(la0) * sinf(lam) * cosf(b));
        float lo = lo0 + atan2f(sinf(b) * sinf(lam) * cosf(la0), cosf(lam) - sinf(la0) * sinf(la));
        int x, y;
        if (globeXY(g, la / D2R, lo / D2R, x, y)) { if (fh && (k % 2)) spr.drawLine(fx, fy, x, y, scale565(C_HOT(), 60)); fx = x; fy = y; fh = true; }
        else fh = false;
      }
    }
    int x, y;
    if (globeXY(g, sp[i].lat, sp[i].lon, x, y)) {
      spr.fillRect(x - 2, y - 2, 5, 5, main ? C_HOT() : C_ACC());
      spr.drawFastHLine(x - 6, y, 13, main ? C_HOT() : C_ACC());
      spr.setTextDatum(BL_DATUM);
      spr.setTextColor(main ? C_HOT() : UI_LABEL());
      txt(g_sats[i].name, x + 5, y - 3, 1);
    }
  }

  // ---- right: satellite data and next pass ----
  const int px = 160;
  char buf[48], b2[16];
  spr.setTextDatum(TL_DATUM);
  int si = satIdx >= 0 ? satIdx : 0;
  if (!timeOk) {
    spr.setTextColor(C_ALERT()); txt(L("TIME UNKNOWN", "ВРЕМЯ НЕИЗВЕСТНО"), px, UI_TOP + 4, 2);
    spr.setTextColor(UI_LABEL()); txt(L("CONNECT ONCE TO", "ОДИН РАЗ ПОДКЛЮЧИТЕ"), px, UI_TOP + 26, 1);
    txt(L("TENGEN DESKTOP OR TABLET", "TENGEN DESKTOP ИЛИ ПЛАНШЕТ"), px, UI_TOP + 38, 1);
  } else if (g_nSats == 0) {
    spr.setTextColor(C_ALERT()); txt(L("NO ORBITS", "НЕТ ОРБИТ"), px, UI_TOP + 4, 2);
    spr.setTextColor(UI_LABEL()); txt(L("TENGEN DESKTOP LOADS THEM:", "ИХ ЗАГРУЖАЕТ TENGEN DESKTOP:"), px, UI_TOP + 26, 1);
    txt(L("START IT ON THE COMPUTER", "ЗАПУСТИТЕ ЕГО НА КОМПЬЮТЕРЕ"), px, UI_TOP + 38, 1);
    txt(L("(NEEDS INTERNET ONCE)", "(НУЖЕН ИНТЕРНЕТ ОДИН РАЗ)"), px, UI_TOP + 50, 1);
  } else if (spOk[si]) {
    const SatPos& p = sp[si];
    spr.setTextColor(C_HOT()); txt(g_sats[si].name, px, UI_TOP + 2, 4);
    snprintf(buf, sizeof(buf), L("ALT %d KM   %.2f KM/S", "ВЫС %d КМ   %.2f КМ/С"), (int)p.altKm, p.speedKms);
    spr.setTextColor(UI_LABEL()); txt(buf, px, UI_TOP + 28, 1);
    snprintf(buf, sizeof(buf), "%.1f%c  %.1f%c", fabsf(p.lat), p.lat >= 0 ? 'N' : 'S', fabsf(p.lon), p.lon >= 0 ? 'E' : 'W');
    txt(buf, px, UI_TOP + 40, 1);
    // next pass over the city: recomputed once a minute (or when the satellite/city changes)
    static int lastSi = -1, lastCity = -1;
    if (t - s_passAt > 60 || lastSi != si || lastCity != g_city || (s_pass.found && t > s_pass.set)) {
      s_pass = satNextPass(g_sats[si], t, c.lat, c.lon, 10, 24);
      s_passAt = t; lastSi = si; lastCity = g_city;
    }
    float el, az; satLook(p, c.lat, c.lon, el, az);
    const char* const DIR8[8] = { L("N", "С"), L("NE", "СВ"), L("E", "В"), L("SE", "ЮВ"), L("S", "Ю"), L("SW", "ЮЗ"), L("W", "З"), L("NW", "СЗ") };
    if (el >= 10) {
      spr.setTextColor(C_HOT()); txt(L("OVERHEAD NOW", "НАД ВАМИ"), px, UI_TOP + 58, 2);
      snprintf(buf, sizeof(buf), L("LOOK %s, %d DEG UP", "СМОТРИТЕ %s, %d° ВВЕРХ"), DIR8[((int)(az + 22.5f) / 45) % 8], (int)el);
      spr.setTextColor(C_ACC()); txt(buf, px, UI_TOP + 76, 1);
    } else if (s_pass.found) {
      spr.setTextColor(UI_LABEL()); txt(L("NEXT PASS OVER YOU", "СЛЕДУЮЩИЙ ПРОЛЁТ"), px, UI_TOP + 58, 1);
      hhmm(s_pass.rise, b2, sizeof(b2));
      long in = (long)(s_pass.rise - t);
      snprintf(buf, sizeof(buf), "%s", b2);
      spr.setTextColor(C_HOT()); txt(buf, px, UI_TOP + 70, 4);
      snprintf(buf, sizeof(buf), L("IN %ldH %02ldM", "ЧЕРЕЗ %ld Ч %02ld МИН"), in / 3600, in / 60 % 60);
      spr.setTextColor(C_ACC()); txt(buf, px, UI_TOP + 96, 1);
      snprintf(buf, sizeof(buf), L("UP TO %d DEG, FROM %s", "ДО %d°, С %s"), (int)s_pass.maxEl, DIR8[((int)(s_pass.azRise + 22.5f) / 45) % 8]);
      txt(buf, px, UI_TOP + 106, 1);
    } else {
      spr.setTextColor(UI_LABEL()); txt(L("NO PASS IN 24 H", "ЗА 24 Ч ПРОЛЁТОВ НЕТ"), px, UI_TOP + 58, 1);
    }
    float age = satAgeDays(g_sats[si], t);
    snprintf(buf, sizeof(buf), L("ORBIT DATA %.1f DAYS OLD", "ОРБИТЫ ОТ %.1f ДН. НАЗАД"), age);
    spr.setTextColor(age > 7 ? C_ALERT() : UI_DIM()); txt(buf, px, UI_TOP + 120, 1);
  }
  const char* follow = s_gFollow == 1 ? L("home", "дом") : (satIdx >= 0 && satIdx < g_nSats ? g_sats[satIdx].name : "ISS");
  snprintf(buf, sizeof(buf), L("following %s", "слежу: %s"), follow);
  uiHeader(L("Globe", "Глобус"), buf);
  uiFooter(L("wheel: turn  ·  click: follow", "колесо: поворот · нажать: цель"), L("side: back", "кнопка: назад"));
  present();
}
