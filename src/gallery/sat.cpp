// ============================================================
//  sat.cpp — TLE parsing and simplified orbit propagation (Kepler + J2)
// ============================================================
#include "gallery/sat.h"
#include "core/jsonutil.h"
#include "gallery/astro.h"
#include <Preferences.h>

Sat g_sats[SAT_MAX];
int g_nSats = 0;

static const double MU = 398600.4418;          // km³/s²
static const double RE = 6378.137, J2 = 1.08262668e-3;
static const double TWO_PI_ = 2 * PI, D2R = PI / 180.0;

static double fld(const char* s, int from, int len) {      // number from TLE columns (1-based)
  char b[24]; int n = min(len, 23);
  memcpy(b, s + from - 1, n); b[n] = 0;
  return atof(b);
}
static bool parseTle(const char* name, const char* l1, const char* l2, Sat& s) {
  if (strlen(l1) < 63 || strlen(l2) < 63 || l1[0] != '1' || l2[0] != '2') return false;
  int yy = (int)fld(l1, 19, 2);
  double doy = fld(l1, 21, 12);
  int year = yy < 57 ? 2000 + yy : 1900 + yy;
  // Unix time of the start of the year
  long days = 0;
  for (int y = 1970; y < year; y++) days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
  s.epochUnix = (days + doy - 1) * 86400.0;
  double ndot2 = fld(l1, 34, 10);                          // (dn/dt)/2, rev/day²
  s.inc   = fld(l2, 9, 8) * D2R;
  s.raan0 = fld(l2, 18, 8) * D2R;
  char e[10] = "0."; memcpy(e + 2, l2 + 26, 7); e[9] = 0;
  s.ecc   = atof(e);
  s.argp0 = fld(l2, 35, 8) * D2R;
  s.m0    = fld(l2, 44, 8) * D2R;
  double revDay = fld(l2, 53, 11);
  s.n0    = revDay * TWO_PI_ / 1440.0;                     // rad/min
  s.ndot2 = ndot2 * TWO_PI_ / (1440.0 * 1440.0);
  double nSec = s.n0 / 60.0;
  s.a = pow(MU / (nSec * nSec), 1.0 / 3.0);
  strncpy(s.name, name, sizeof(s.name) - 1); s.name[sizeof(s.name) - 1] = 0;
  s.ok = revDay > 0.5 && s.ecc < 0.9;
  if (s.ok) {
    s.cosi = cos(s.inc); s.sini = sin(s.inc);
    double p = s.a * (1.0 - s.ecc * s.ecc);
    double k = 1.5 * J2 * (RE / p) * (RE / p) * s.n0;
    s.raanRate = -k * s.cosi;
    s.argpRate =  k * (2.0 - 2.5 * s.sini * s.sini);
    s.sq1pe = sqrt(1.0 + s.ecc);
    s.sq1me = sqrt(1.0 - s.ecc);
  }
  return s.ok;
}

// ---------- storage ----------
static void satSave(int i, const char* name, const char* l1, const char* l2) {
  Preferences p; p.begin("sats", false);
  char k[8];
  snprintf(k, sizeof(k), "n%d", i); p.putString(k, name);
  snprintf(k, sizeof(k), "a%d", i); p.putString(k, l1);
  snprintf(k, sizeof(k), "b%d", i); p.putString(k, l2);
  p.end();
}
void satBegin() {
  Preferences p; p.begin("sats", true);
  g_nSats = 0;
  for (int i = 0; i < SAT_MAX; i++) {
    char k[8];
    snprintf(k, sizeof(k), "n%d", i); String n = p.getString(k, "");
    snprintf(k, sizeof(k), "a%d", i); String a = p.getString(k, "");
    snprintf(k, sizeof(k), "b%d", i); String b = p.getString(k, "");
    if (n.length() && parseTle(n.c_str(), a.c_str(), b.c_str(), g_sats[g_nSats])) g_nSats++;
  }
  p.end();
}

// "TLE\t<name>\t<line1>\t<line2>"
void satFromLine(char* line) {
  char* save = nullptr;
  char* f0 = strtok_r(line, "\t", &save);
  char* name = strtok_r(nullptr, "\t", &save);
  char* l1 = strtok_r(nullptr, "\t", &save);
  char* l2 = strtok_r(nullptr, "\t", &save);
  if (!f0 || !name || !l1 || !l2) return;
  Sat s;
  if (!parseTle(name, l1, l2, s)) return;
  int slot = -1;
  for (int i = 0; i < g_nSats; i++) if (!strcmp(g_sats[i].name, s.name)) slot = i;
  if (slot < 0) { if (g_nSats >= SAT_MAX) return; slot = g_nSats++; }
  bool changed = fabs(g_sats[slot].epochUnix - s.epochUnix) > 1;
  g_sats[slot] = s;
  if (changed) satSave(slot, name, l1, l2);                  // save only new elements
}

float satAgeDays(const Sat& s, double t) { return (float)((t - s.epochUnix) / 86400.0); }

// ---------- position ----------
// Fast ECEF-only propagation (skips asin/atan2/speed when scanning for passes)
static bool satEcef(const Sat& s, double t, double& xe, double& ye, double& ze, double* rOut = nullptr) {
  if (!s.ok) return false;
  double tm = (t - s.epochUnix) * (1.0 / 60.0);              // minutes since epoch
  double raan = s.raan0 + s.raanRate * tm;
  double argp = s.argp0 + s.argpRate * tm;
  double M = fmod(s.m0 + s.n0 * tm + s.ndot2 * tm * tm, TWO_PI_);
  if (M < 0) M += TWO_PI_;
  double E = M;
  for (int i = 0; i < 6; i++) {
    double dE = (E - s.ecc * sin(E) - M) / (1.0 - s.ecc * cos(E));
    E -= dE;
    if (fabs(dE) < 1e-7) break;
  }
  double nu = 2.0 * atan2(s.sq1pe * sin(E * 0.5), s.sq1me * cos(E * 0.5));
  double r = s.a * (1.0 - s.ecc * cos(E));
  if (rOut) *rOut = r;
  double u = argp + nu;
  double cu = cos(u), su = sin(u), cO = cos(raan), sO = sin(raan);
  double x = r * (cO * cu - sO * su * s.cosi);
  double y = r * (sO * cu + cO * su * s.cosi);
  ze = r * (su * s.sini);
  double g = gmstDeg(julianDate(t)) * D2R;
  double cg = cos(g), sg = sin(g);
  xe = x * cg + y * sg;
  ye = -x * sg + y * cg;
  return true;
}

bool satPos(const Sat& s, double t, SatPos& out) {
  double xe, ye, ze, r;
  if (!satEcef(s, t, xe, ye, ze, &r)) return false;
  out.x = xe; out.y = ye; out.z = ze;
  double rr = sqrt(xe * xe + ye * ye + ze * ze);
  out.lat = (float)(asin(constrain(ze / rr, -1.0, 1.0)) / D2R);
  out.lon = (float)(atan2(ye, xe) / D2R);
  out.altKm = (float)(rr - RE);
  out.speedKms = (float)sqrt(MU * (2.0 / r - 1.0 / s.a));
  return true;
}

struct ObsBasis { double ox, oy, oz, ex, ey, nx, ny, nz, ux, uy, uz; };
static ObsBasis makeObs(float lat, float lon) {
  double la = lat * D2R, lo = lon * D2R;
  double cla = cos(la), sla = sin(la), clo = cos(lo), slo = sin(lo);
  return { RE * cla * clo, RE * cla * slo, RE * sla,
           -slo, clo,
           -sla * clo, -sla * slo, cla,
            cla * clo,  cla * slo, sla };
}
static inline float obsEl(const ObsBasis& o, double px, double py, double pz, float* azOut = nullptr) {
  double rx = px - o.ox, ry = py - o.oy, rz = pz - o.oz;
  double u = o.ux * rx + o.uy * ry + o.uz * rz;
  double rng = sqrt(rx * rx + ry * ry + rz * rz);
  float el = (float)(asin(constrain(u / rng, -1.0, 1.0)) / D2R);
  if (azOut) {
    double e = o.ex * rx + o.ey * ry;
    double n = o.nx * rx + o.ny * ry + o.nz * rz;
    *azOut = (float)fmod(atan2(e, n) / D2R + 360.0, 360.0);
  }
  return el;
}

void satLook(const SatPos& p, float lat, float lon, float& el, float& az) {
  ObsBasis o = makeObs(lat, lon);
  el = obsEl(o, p.x, p.y, p.z, &az);
}

Pass satNextPass(const Sat& s, double from, float lat, float lon, float minEl, float hours) {
  Pass ps = { false, 0, 0, 0, 0 };
  if (!s.ok) return ps;
  ObsBasis ob = makeObs(lat, lon);
  double end = from + hours * 3600.0;
  bool up = false;
  // 60 s coarse scan (any pass >= 10 deg lasts several minutes), with 2 s refinement on rise/peak/set
  for (double t = from; t < end; t += 60.0) {
    double xe, ye, ze;
    if (!satEcef(s, t, xe, ye, ze)) return ps;
    float az = 0;
    float el = obsEl(ob, xe, ye, ze, t == from ? &az : nullptr);
    if (el >= minEl) {
      if (!up) {
        double tr = t;
        if (t == from) {
          ps.azRise = az; tr = from;
        } else {
          for (double q = t - 60.0; q <= t; q += 2.0) {
            satEcef(s, q, xe, ye, ze);
            float a2 = 0, e2 = obsEl(ob, xe, ye, ze, &a2);
            if (e2 >= minEl) { tr = q; ps.azRise = a2; break; }
          }
        }
        ps.rise = tr; up = true; ps.found = true;
      }
      // sample around t in 10 s steps so peak elevation is accurate
      for (double q = max(from, t - 50.0); q <= min(end, t + 50.0); q += 10.0) {
        satEcef(s, q, xe, ye, ze);
        float eq = obsEl(ob, xe, ye, ze);
        if (eq > ps.maxEl) ps.maxEl = eq;
      }
    } else if (up) {
      double ts = t;
      for (double q = t - 60.0; q <= t; q += 2.0) {
        satEcef(s, q, xe, ye, ze);
        if (obsEl(ob, xe, ye, ze) < minEl) { ts = q; break; }
      }
      ps.set = ts;
      return ps;
    }
  }
  if (up) ps.set = end;
  return ps;
}

// for the tablet: orbit elements; the browser does the rest (same Kepler + J2)
// [[name,epoch_unix,n0,ndot2,inc,raan0,ecc,argp0,m0,a],...]
void satJson(String& o) {
  o += '[';
  for (int i = 0; i < g_nSats; i++) {
    const Sat& s = g_sats[i];
    if (i) o += ',';
    o += '['; jsonStr(o, s.name); o += ','; o += String(s.epochUnix, 0);
    const double v[] = { s.n0, s.ndot2, s.inc, s.raan0, s.ecc, s.argp0, s.m0, s.a };
    for (double x : v) { char b[24]; snprintf(b, sizeof(b), ",%.10g", x); o += b; }
    o += ']';
  }
  o += ']';
}
