// ============================================================
//  ir_screens.cpp — IR screen: big protocol, likely brand,
//  address/command, last presses and the pulse burst itself.
// ============================================================
#include "probe/ir.h"
#include "core/lang.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/settings.h"
#include "core/timeutil.h"


// remote with IR waves (menu icon)
void iconRemote(int x, int y, uint16_t c, uint16_t bg) {
  spr.drawSmoothRoundRect(x - 9, y - 10, 5, 3, 18, 33, c, bg);
  spr.fillSmoothCircle(x, y - 3, 3, c, bg);
  for (int r = 0; r < 3; r++) for (int k = -1; k <= 1; k++) spr.fillRect(x - 1 + k * 5, y + 6 + r * 5, 3, 3, c);
  for (int i = 0; i < 2; i++) {
    int r = 7 + i * 6;
    spr.drawArc(x, y - 13, r, r - 2, 135, 225, c, bg, true);   // waves upward
  }
}

static void hex(char* b, size_t n, uint32_t v, int digits) { snprintf(b, n, "0x%0*lX", digits, (unsigned long)v); }

// pulse burst: flashes = high level; a sweep runs left to right after reception
static void drawPulses(int x, int y, int w, int h, uint32_t now) {
  const IrModel& m = g_ir;
  uint16_t line = UI_LINE(), acc = C_ACC(), hot = C_HOT();
  uiFrame(x - 2, y - 2, w + 4, h + 4, line);
  if (m.rawN < 2) return;
  uint32_t total = 0;
  for (int i = 0; i < m.rawN; i++) total += m.raw[i];
  if (!total) return;
  float sc = (float)w / total;
  float sweep = constrain(msSince(now, m.last.t) / 450.0f, 0.0f, 1.0f);   // 0..1 = sweep
  int sx = x + (int)(sweep * w);
  int yH = y + 3, yL = y + h - 3;
  float fx = x;
  int prevY = yL;
  for (int i = 0; i < m.rawN; i++) {
    bool mark = (i % 2) == 0;
    int x0 = (int)fx; fx += m.raw[i] * sc; int x1 = max(x0 + 1, (int)fx);
    if (x1 > x + w) x1 = x + w;
    int yy = mark ? yH : yL;
    uint16_t c = x0 < sx ? (mark ? hot : acc) : scale565(acc, 35);
    spr.drawFastVLine(x0, min(prevY, yy), abs(prevY - yy) + 1, c);
    spr.drawFastHLine(x0, yy, x1 - x0 + 1, c);
    prevY = yy;
  }
  if (sweep < 1) spr.drawFastVLine(sx, y, h, hot);   // sweep cursor
}

// idle: meditative Tengen receiver Eye with inward-converging 38 kHz waves
static void drawIdle(uint32_t now) {
  int W = spr.width(), cx = W / 2, cy = 72;
  uint16_t acc = C_ACC(), line = UI_LINE();
  spr.drawArc(cx, cy, 30, 28, 25, 335, scale565(acc, 65), C_BG(), true);
  spr.drawFastHLine(cx - 38, cy, 5, line); spr.drawFastHLine(cx + 34, cy, 5, line);
  spr.drawFastVLine(cx, cy - 36, 5, line); spr.drawFastVLine(cx, cy + 32, 5, line);
  for (int i = 0; i < 3; i++) {
    float ph = fmodf(now / 1500.0f + i / 3.0f, 1.0f);
    int r = 8 + (int)((1.0f - ph) * 20);
    uint16_t c = scale565(acc, (int)(85 * sinf(ph * PI)));
    spr.drawCircle(cx, cy, r, c);
  }
  drawMarker(cx, cy, C_HOT(), false);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_HOT());
  txt(L("POINT ANY REMOTE AT TENGEN", "НАПРАВЬТЕ ПУЛЬТ НА TENGEN"), cx, 118, 1);
  spr.setTextColor(UI_DIM());
  txt(L("PRESS A BUTTON TO SEE WHOSE REMOTE IT IS", "НАЖМИТЕ КНОПКУ - УЗНАЕМ, ЧЕЙ ЭТО ПУЛЬТ"), cx, 132, 1);
}

void drawIr(uint32_t now) {
  const IrModel& m = g_ir;
  int W = spr.width();
  char buf[48];
  spr.fillSprite(C_BG());
  if (m.on && !m.ok) {
    uiHeader(L("IR", "ИК"), L("error", "ошибка"), true);
    uiEmpty(L("receiver did not start", "приёмник не запустился"), L("go back and try again", "выйдите и попробуйте снова"));
    uiFooter("", L("side: back", "кнопка: назад"));
    present();
    return;
  }
  if (m.presses) snprintf(buf, sizeof(buf), "%lu %s", (unsigned long)m.presses, PL((int)m.presses, "press", "presses", "нажатие", "нажатия", "нажатий")); else snprintf(buf, sizeof(buf), "%s", L("listening", "слушаю"));
  uiHeader(L("IR · whose remote?", "ИК · чей пульт?"), buf, false);
  if (!m.have) {
    drawIdle(now);
    uiFooter(L("receive only", "только приём"), L("side: back", "кнопка: назад"));
    present();
    return;
  }
  const IrCode& c = m.last;
  bool fresh = msSince(now, c.t) < 400;
  // big protocol; on repeat a HOLD badge
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(fresh ? C_HOT() : UI_LABEL());
  txt(c.proto, UI_M + 4, 26, 4);
  int pw = txtW(c.proto, 4);
  if (c.rep && msSince(now, c.t) < 300) {
    int bx = UI_M + 4 + pw + 8;
    hudPanel(bx, 30, 38, 15, 5, C_ACC(), true, C_BG());
    spr.setTextColor(C_BG());
    spr.setTextDatum(MC_DATUM);
    txt(L("HOLD", "ДЕРЖ"), bx + 19, 38, 1);
    spr.setTextDatum(TL_DATUM);
  }
  // brand: "probably"
  bool known = c.brand && strcmp(c.brand, IR_UNKNOWN) && strcmp(c.brand, IR_UNKNOWN_REMOTE) && strcmp(c.brand, IR_NEC_GENERIC);
  spr.setTextColor(C_ACC());
  char br[40]; fitText(c.brand ? c.brand : "", br, sizeof(br), 186, 2);
  txt(br, UI_M + 4, 54, 2);
  spr.setTextColor(UI_DIM());
  txt(known ? L("LIKELY · GUESSED BY ADDRESS", "ВЕРОЯТНО · ПО АДРЕСУ") : L("BRAND NOT IN TABLE", "МАРКИ НЕТ В ТАБЛИЦЕ"), UI_M + 4, 72, 1);
  // address / command / bits
  bool raw = !strcmp(c.proto, "RAW");
  char a[16], k[16], b[8];
  if (raw) { snprintf(a, sizeof a, "--"); hex(k, sizeof k, c.cmd, 8); }
  else { hex(a, sizeof a, c.addr, c.addr > 0xFF ? 4 : 2); hex(k, sizeof k, c.cmd, c.cmd > 0xFF ? 4 : 2); }
  snprintf(b, sizeof b, "%u", c.bits);
  uiReadout(UI_M + 4, 84, L("addr", "адрес"), a, C_HOT());
  uiReadout(UI_M + 66, 84, raw ? L("print", "отпечаток") : L("cmd", "команда"), k, C_HOT());
  uiReadout(UI_M + 152, 84, raw ? L("pulses", "импульсы") : L("bits", "биты"), b, UI_LABEL());

  // press history on the right
  int hx = 212, hy = 26;
  spr.drawFastVLine(hx - 6, 26, 80, UI_LINE());
  spr.setTextColor(UI_DIM());
  txt(L("LAST", "ПОСЛЕДНИЕ"), hx, hy, 1);
  for (int i = 0; i < m.histN && i < 7; i++) {
    const IrCode& h = m.hist[i];
    int y = hy + 12 + i * 11;
    uint32_t age = msSince(now, h.t) / 1000;
    if (h.proto[0] == 'R' && h.proto[1] == 'A') snprintf(buf, sizeof buf, "RAW %04lX", (unsigned long)(h.cmd & 0xFFFF));
    else snprintf(buf, sizeof buf, "%.7s %lX:%lX", h.proto, (unsigned long)h.addr, (unsigned long)h.cmd);
    char f[20]; fitText(buf, f, sizeof f, 74, 1);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(i == 0 ? C_HOT() : scale565(UI_LABEL(), 100 - i * 9));
    txt(f, hx, y, 1);
    snprintf(buf, sizeof buf, age < 60 ? L("%lus", "%luс") : L("%lum", "%luм"), (unsigned long)(age < 60 ? age : age / 60));
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(UI_DIM());
    txt(buf, W - UI_M - 2, y, 1);
  }
  spr.setTextDatum(TL_DATUM);
  drawPulses(UI_M + 4, 116, W - 2 * UI_M - 8, 30, now);
  uiFooter(L("click: clear", "нажать: очистить"), L("side: back", "кнопка: назад"));
  present();
}
