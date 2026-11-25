// ============================================================
//  nfc_model.cpp — what card it is: from ATQA/SAK (+ the PPSE reply and
//  GET_VERSION), tag NDEF record parsing, tap history.
//  No hardware, so the simulator builds it too.
// ============================================================
#include "probe/nfc.h"
#include "core/lang.h"

NfcModel g_nfc;

static void put(NfcCard& c, const char* fam, const char* hint) {
  snprintf(c.fam, sizeof(c.fam), "%s", fam);
  snprintf(c.hint, sizeof(c.hint), "%s", hint);
}

void nfcClassify(NfcCard& c, int ulVer) {
  if (c.felica) { put(c, "FeliCa", L("Japan transit / e-money · likely", "проезд / e-money (Япония) · вероятно")); return; }
  if (c.bank)   { put(c, L("Bank card (EMV)", "Банковская карта (EMV)"), L("tap-to-pay card or phone", "бесконтактная оплата")); return; }
  uint8_t sak = c.sak & 0x7F;                    // bit 7 (0x80) on some is "cascade", not a type
  switch (c.sak) {
    case 0x09: put(c, "MIFARE Mini", L("old transit / access card", "старая проездная / пропуск")); return;
    case 0x08: case 0x88: put(c, "MIFARE Classic 1K", L("transit pass / door key · likely", "проездной / ключ от двери · вероятно")); return;
    case 0x18: put(c, "MIFARE Classic 4K", L("transit / campus card · likely", "проездная / студенческая · вероятно")); return;
    case 0x10: put(c, "MIFARE Plus 2K", L("transit / access card · likely", "проездная / пропуск · вероятно")); return;
    case 0x11: put(c, "MIFARE Plus 4K", L("transit / access card · likely", "проездная / пропуск · вероятно")); return;
    case 0x28: put(c, L("Classic + smart chip", "Classic + смарт-чип"), L("transit + smart card · likely", "проездная + смарт-карта · вероятно")); return;
    case 0x38: put(c, L("Classic 4K + smart chip", "Classic 4K + смарт-чип"), L("transit + smart card · likely", "проездная + смарт-карта · вероятно")); return;
  }
  if (sak == 0x00) {
    switch (ulVer) {
      case 0x0F: put(c, "NTAG213", L("sticker / tag · 144 bytes", "наклейка / метка · 144 байта")); return;
      case 0x11: put(c, "NTAG215", L("tag / game figure · 504 bytes", "метка / игровая фигурка · 504 байта")); return;
      case 0x13: put(c, "NTAG216", L("sticker / tag · 888 bytes", "наклейка / метка · 888 байт")); return;
      case 0x0B: put(c, "Ultralight EV1", L("single-ride ticket / tag · likely", "разовый билет / метка · вероятно")); return;
      case 0x0E: put(c, "Ultralight EV1", L("ticket / tag · likely", "билет / метка · вероятно")); return;
    }
    put(c, "Ultralight / NTAG", L("ticket, sticker or tag · likely", "билет, наклейка или метка · вероятно")); return;
  }
  if (c.sak & 0x20) {
    if (c.atqa == 0x0344) { put(c, "MIFARE DESFire", L("transit / access card · likely", "проездная / пропуск · вероятно")); return; }
    put(c, L("Smart card (ISO-DEP)", "Смарт-карта (ISO-DEP)"), L("ID / access / transit · unknown", "удостоверение / пропуск / проезд · неизвестно")); return;
  }
  put(c, L("ISO 14443-A tag", "Метка ISO 14443-A"), L("unknown chip", "неизвестный чип"));
}

// ---------- NDEF ----------
static const char* const URI_PFX[] = {
  "", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:", "ftp://anonymous:anonymous@",
  "ftp://ftp.", "ftps://", "sftp://", "smb://", "nfs://", "ftp://", "dav://", "news:", "telnet://", "imap:",
  "rtsp://", "urn:", "pop:", "sip:", "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://", "tcpobex://",
  "irdaobex://", "file://", "urn:epc:id:", "urn:epc:tag:", "urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:",
};

static void addRec(NfcCard& c, const char* pre, const uint8_t* p, int n) {
  if (c.nNdef >= NFC_NDEF_N) return;
  char* o = c.ndef[c.nNdef++];
  int k = snprintf(o, NFC_NDEF_LEN, "%s", pre);
  for (int i = 0; i < n && k < NFC_NDEF_LEN - 1; i++) o[k++] = p[i] < 0x20 ? ' ' : (char)p[i];
  if (k > NFC_NDEF_LEN - 1) k = NFC_NDEF_LEN - 1;
  o[k] = 0;
  // don't leave half of a Cyrillic letter at the end
  while (k > 0 && ((uint8_t)o[k - 1] & 0xC0) == 0xC0) o[--k] = 0;
}

static void parseMsg(const uint8_t* p, int n, NfcCard& c) {
  int i = 0;
  while (i + 3 <= n && c.nNdef < NFC_NDEF_N) {
    uint8_t h = p[i++];
    uint8_t tnf = h & 7; bool sr = h & 0x10, il = h & 0x08;
    uint8_t tl = p[i++];
    uint32_t pl;
    if (sr) { if (i >= n) return; pl = p[i++]; }
    else    { if (i + 4 > n) return; pl = (uint32_t)p[i] << 24 | p[i + 1] << 16 | p[i + 2] << 8 | p[i + 3]; i += 4; }
    uint8_t idl = 0;
    if (il) { if (i >= n) return; idl = p[i++]; }
    if (i + tl + idl > n) return;
    const uint8_t* type = p + i; i += tl + idl;
    if (i > n) return;
    int rem = n - i;
    int avail = pl > (uint32_t)rem ? rem : (int)pl;
    const uint8_t* pay = p + i;
    if (tnf == 1 && tl == 1 && type[0] == 'U' && avail >= 1) {
      uint8_t pc = pay[0];
      addRec(c, pc < sizeof(URI_PFX) / sizeof(URI_PFX[0]) ? URI_PFX[pc] : "", pay + 1, avail - 1);
    } else if (tnf == 1 && tl == 1 && type[0] == 'T' && avail >= 1) {
      int ll = pay[0] & 0x3F;
      if (1 + ll <= avail) addRec(c, "", pay + 1 + ll, avail - 1 - ll);
    } else if (tnf == 2) {                                   // MIME: show only the type (vCard, Wi-Fi: personal/password)
      bool wifi = tl >= 19 && !memcmp(type, "application/vnd.wfa", 19);
      if (wifi) addRec(c, L("Wi-Fi settings (hidden)", "настройки Wi-Fi (скрыто)"), nullptr, 0);
      else addRec(c, L("data: ", "данные: "), type, tl);
    } else if (tnf == 4) {
      addRec(c, L("app: ", "приложение: "), type, tl);                          // android.com:pkg etc.
    }
    if (pl > (uint32_t)rem) break;
    i += (int)pl;
    if (h & 0x40) break;                                     // ME = last record
  }
}

void nfcParseNdef(const uint8_t* m, int n, NfcCard& c) {
  c.nNdef = 0;
  int i = 0;
  while (i < n) {
    uint8_t t = m[i++];
    if (t == 0x00) continue;                                 // NULL TLV
    if (t == 0xFE) return;                                   // end
    if (i >= n) return;
    uint32_t len = m[i++];
    if (len == 0xFF) { if (i + 2 > n) return; len = (uint32_t)m[i] << 8 | m[i + 1]; i += 2; }
    int rem = n - i;
    if (t == 0x03) { parseMsg(m + i, len > (uint32_t)rem ? rem : (int)len, c); return; }
    if (len > (uint32_t)rem) return;
    i += (int)len;
  }
}

void nfcUidMasked(const NfcCard& c, char* out, size_t n, bool utf8Dots) {
  int k = 0;
  out[0] = 0;
  for (int i = 0; i < 2 && i < c.uidLen; i++) k += snprintf(out + k, n - k, "%02X", c.uid[i]);
  if (c.uidLen > 2 && (size_t)k + 7 < n) k += snprintf(out + k, n - k, utf8Dots ? "\xE2\x80\xA2\xE2\x80\xA2" : "..");
}

void nfcCommit(const NfcCard& c0, uint32_t now) {
  NfcModel& m = g_nfc;
  NfcCard c = c0; c.t = now;
  m.last = c; m.have = true; m.present = true; m.lastSeen = now;
  m.taps++; m.gen++;
  memmove(m.hist + 1, m.hist, sizeof(NfcCard) * (NFC_HIST - 1));
  m.hist[0] = c;
  if (m.histN < NFC_HIST) m.histN++;
}
