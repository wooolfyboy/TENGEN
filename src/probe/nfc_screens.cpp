// ============================================================
//  nfc_screens.cpp — NFC screen: card silhouette, rings for the "tap",
//  card type, start of the UID, open tag records.
// ============================================================
#include "probe/nfc.h"
#include "core/lang.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/settings.h"
#include "core/timeutil.h"


// card with waves (menu icon)
void iconCard(int x, int y, uint16_t c, uint16_t bg) {
  spr.drawSmoothRoundRect(x - 17, y - 6, 3, 2, 30, 20, c, bg);
  spr.fillRect(x - 12, y, 7, 6, c);                          // chip
  for (int i = 0; i < 3; i++) {
    int r = 6 + i * 6;
    spr.drawArc(x + 2, y - 10, r, r - 2, 135, 225, c, bg, true);   // waves above the card
  }
}

// card silhouette: cut-corner HUD card; chip = smart/bank, bank = "not read" instead of a number
static void card(int x, int y, int w, int h, uint16_t edge, uint16_t fill, bool chip, bool bank) {
  hudPanel(x, y, w, h, 8, fill, true, C_BG());
  hudPanel(x, y, w, h, 8, edge, false, C_BG());
  for (int i = 0; i < 3; i++) spr.drawArc(x + w - 22, y + 16, 3 + i * 4, 2 + i * 4, 225, 315, edge, fill, true);   // contactless symbol
  if (chip) {
    int cx = x + 12, cy = y + h / 2 - 9;
    spr.fillRoundRect(cx, cy, 20, 15, 3, C_ACC());
    spr.drawFastHLine(cx, cy + 7, 20, fill); spr.drawFastVLine(cx + 10, cy, 15, fill);
  }
  if (bank) for (int g = 0; g < 4; g++) for (int k = 0; k < 4; k++)
    spr.fillCircle(x + 12 + g * (w - 24) / 4 + k * 5, y + h - 16, 1, edge);   // •••• •••• = number not read
}

static void rings(int cx, int cy, float ph, uint16_t acc) {
  for (int i = 0; i < 3; i++) {
    float p = fmodf(ph + i / 3.0f, 1.0f);
    int r = 10 + (int)(p * 46);
    spr.drawCircle(cx, cy, r, scale565(acc, (int)(100 * (1 - p))));
  }
}

static void drawIdle(uint32_t now) {
  int W = spr.width();
  uint16_t acc = C_ACC();
  // card flies to the stick, touch = rings
  float t = fmodf(now / 2200.0f, 1.0f);
  float slide = t < 0.5f ? t / 0.5f : 1.0f;
  slide = 1 - (1 - slide) * (1 - slide);
  int cx = W / 2 + 40, cy = 74;
  if (t >= 0.5f) rings(cx, cy, (t - 0.5f) * 2, acc);
  spr.fillSmoothCircle(cx, cy, 6, C_HOT(), C_BG());
  int x = (int)(20 + slide * (cx - 44 - 20));
  card(x, cy - 26, 88, 54, scale565(UI_LABEL(), 50 + (int)(slide * 50)), UI_PANEL(), true, false);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(UI_LABEL());
  txt(L("TOUCH A CARD TO TENGEN", "ПРИЛОЖИТЕ КАРТУ К TENGEN"), W / 2, 122, 1);
  spr.setTextColor(UI_DIM());
  txt(L("READ ONLY: CARD TYPE AND PUBLIC TAG DATA", "ТОЛЬКО ЧТЕНИЕ: ТИП КАРТЫ И ОТКРЫТЫЕ ДАННЫЕ"), W / 2, 136, 1);
}

void drawNfc(uint32_t now) {
  const NfcModel& m = g_nfc;
  int W = spr.width();
  char buf[64];
  spr.fillSprite(C_BG());
  if (m.state == NFC_NOREADER) {
    uiHeader("NFC", L("no chip", "нет чипа"), true);
    uiEmpty(L("NFC chip not found", "чип NFC не найден"), L("this model may have no NFC · click: try again", "у этой модели может не быть NFC · нажать: повтор"));
    uiFooter(L("click: retry", "нажать: повтор"), L("side: back", "кнопка: назад"));
    present();
    return;
  }
  if (m.taps) snprintf(buf, sizeof buf, "%lu %s", (unsigned long)m.taps, PL((int)m.taps, "card", "cards", "карта", "карты", "карт")); else snprintf(buf, sizeof buf, "%s", L("scanning", "поиск"));
  uiHeader(L("NFC · what card?", "NFC · что за карта?"), buf, false);
  if (!m.have) {
    drawIdle(now);
    uiFooter(L("read only", "только чтение"), L("side: back", "кнопка: назад"));
    present();
    return;
  }
  const NfcCard& c = m.last;
  uint32_t age = msSince(now, c.t);
  // card: presented = bright, with touch rings; removed = dim
  int cx = UI_M + 4, cy = 36, cw = 116, ch = 72;
  if (age < 900) rings(cx + cw / 2, cy + ch / 2, age / 900.0f, C_ACC());
  bool chip = c.isoDep || c.bank || c.felica || (c.sak & 0x28);
  card(cx, cy, cw, ch, m.present ? C_ACC() : UI_LINE(), m.present ? UI_PANEL() : C_BG(), chip, c.bank);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(m.present ? C_HOT() : UI_DIM());
  if (m.present) txt(L("ON THE READER", "НА СЧИТЫВАТЕЛЕ"), cx + cw / 2, cy + ch + 8, 1);
  else { uint32_t s = age / 1000; snprintf(buf, sizeof buf, s < 60 ? L("LAST CARD %lus AGO", "КАРТА %lu С НАЗАД") : L("LAST CARD %lum AGO", "КАРТА %lu МИН НАЗАД"), (unsigned long)(s < 60 ? s : s / 60)); txt(buf, cx + cw / 2, cy + ch + 8, 1); }

  // right: type, hint, UID, ATQA/SAK, records
  int tx = 140, tw = W - tx - UI_M - 2;
  spr.setTextDatum(TL_DATUM);
  char f[48];
  spr.setTextColor(C_HOT());
  fitText(c.fam, f, sizeof f, tw, 2);
  txt(f, tx, 26, 2);
  spr.setTextColor(C_ACC());
  char hu[100]; utf8Upper(c.hint, hu, sizeof hu);
  fitText(hu, f, sizeof f, tw, 1);
  txt(f, tx, 44, 1);
  // UID: first two bytes, the rest as dots
  spr.setTextColor(UI_DIM());
  txt("UID", tx, 58, 1);
  snprintf(buf, sizeof buf, c.uidLen >= 2 ? "%02X %02X" : "%02X", c.uid[0], c.uid[1]);
  spr.setTextColor(C_HOT());
  txt(buf, tx, 68, 2);
  int dx = tx + txtW(buf, 2) + 6;
  for (int i = 2; i < c.uidLen && i < 8; i++) spr.fillCircle(dx + (i - 2) * 8, 75, 2, UI_LABEL());
  if (!c.felica) {
    snprintf(buf, sizeof buf, "%04X", c.atqa); uiReadout(tx + 104, 58, "atqa", buf, UI_LABEL());
    snprintf(buf, sizeof buf, "%02X", c.sak);  uiReadout(tx + 146, 58, "sak", buf, UI_LABEL());
  }
  int y = 88;
  if (c.bank) {
    spr.setTextColor(UI_DIM());
    txt(L("NUMBER AND NAME ARE NOT READ", "НОМЕР И ИМЯ НЕ ЧИТАЮТСЯ"), tx, y + 4, 1);
  } else if (c.nNdef) {
    spr.setTextColor(UI_DIM());
    txt(L("ON THE TAG", "НА МЕТКЕ"), tx, y, 1);
    for (int i = 0; i < c.nNdef && i < 2; i++) {
      spr.setTextColor(i == 0 ? C_HOT() : UI_LABEL());
      fitText(c.ndef[i], f, sizeof f, tw, 3);
      txt(f, tx, y + 10 + i * 17, 3);
    }
  } else {
    spr.setTextColor(UI_DIM());
    txt(c.isoDep ? L("PROTECTED CHIP · NOTHING PUBLIC", "ЗАЩИЩЁННЫЙ ЧИП · ОТКРЫТОГО НЕТ") : L("NO PUBLIC RECORDS", "ОТКРЫТЫХ ЗАПИСЕЙ НЕТ"), tx, y + 4, 1);
  }
  // history: last cards
  spr.setTextDatum(TL_DATUM);
  for (int i = 1; i < m.histN && i < NFC_HIST; i++) {
    const NfcCard& h = m.hist[i];
    char u[16]; nfcUidMasked(h, u, sizeof u, false);
    snprintf(buf, sizeof buf, "%s  %s", h.fam, u);
    fitText(buf, f, sizeof f, tw, 1);
    spr.setTextColor(scale565(UI_LABEL(), 100 - i * 20));
    txt(f, tx, 134 + (i - 1) * 10, 1);
    if (i >= 2) break;
  }
  uiFooter(L("read only", "только чтение"), L("side: back", "кнопка: назад"));
  present();
}
