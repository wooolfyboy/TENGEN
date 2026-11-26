// ============================================================
//  TENGEN EAR #2 — Satellite BLE Scanner for M5StickC Plus
//
//  Listens to BLE advertisements, smooths RSSI, and transmits
//  compact ESP-NOW summaries ("TGN1") on Channel 6 to the main
//  T-Embed CC1101 Plus running TENGEN Locator 2x.
//
//  Controls on M5StickC Plus:
//  - Front Button [M5] (GPIO 37): Toggle screen ON/OFF to save battery
//  - Side Button  [B]  (GPIO 39): Force immediate ping / clear stale list
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>
#include <NimBLEDevice.h>
#include <vector>
#include <algorithm>

#define BTN_A_PIN  37
#define BTN_B_PIN  39
#define LED_PIN    10   // active LOW on M5StickC Plus
#define AXP_ADDR   0x34
#define TENGEN_CH  6

// ---------- Shared ESP-NOW Binary Protocol ----------
#pragma pack(push, 1)
struct EarEntry {
  uint8_t mac[6];
  int8_t  rssi;
  uint8_t type;
};
struct EarPacket {
  char     magic[4];   // "TGN1"
  uint8_t  nodeId;     // 1 = M5StickC Plus Ear #2
  uint8_t  batPct;     // 0..100
  uint8_t  flags;      // bit 0: USB/charging
  uint8_t  count;      // 0..28
  uint32_t seq;
  EarEntry entries[28];
};
struct AckPacket {
  char     magic[4];   // "TGNA"
  uint8_t  channel;
  uint8_t  active;
  uint16_t matched;
};
#pragma pack(pop)

// ---------- AXP192 Power Management (M5StickC Plus) ----------
static void axpWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(AXP_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t axpRead8(uint8_t reg) {
  Wire.beginTransmission(AXP_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)AXP_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

static void axpBegin() {
  Wire.begin(21, 22, 400000);
  axpWrite(0x10, 0xFF);   // EXTEN & DC-DC2
  axpWrite(0x28, 0xCC);   // LDO2 (backlight 3.0V) & LDO3 (TFT logic 3.0V)
  axpWrite(0x82, 0xFF);   // Enable all battery V/I ADCs
  axpWrite(0x33, 0xC0);   // Charge 4.2V, 100mA
  axpWrite(0xB8, 0x80);   // Coulomb counter enable
  axpWrite(0x12, 0x4D);   // Enable DCDC1, DCDC3, LDO2, LDO3, EXTEN
  axpWrite(0x36, 0x5C);   // PEK power button timing (4s shutdown)
}

static void axpScreenPower(bool on) {
  uint8_t v = axpRead8(0x12);
  if (on) v |= 0x04; else v &= ~0x04;   // bit 2 = LDO2 (TFT backlight)
  axpWrite(0x12, v);
}

static uint8_t axpBatPct(bool& usbOrChg) {
  uint8_t st0 = axpRead8(0x00);
  usbOrChg = (st0 & 0x24) != 0;         // VBUS present or charging
  uint16_t hi = axpRead8(0x78), lo = axpRead8(0x79);
  float mv = ((hi << 4) | (lo & 0x0F)) * 1.1f;
  int pct = (int)((mv - 3300.0f) * 100.0f / (4150.0f - 3300.0f));
  return (uint8_t)constrain(pct, 0, 100);
}

// ---------- BLE Device Table ----------
struct SeenDev {
  uint8_t  mac[6];
  float    rssiS;
  uint8_t  type;
  uint32_t seenMs;
};

static const int MAX_SEEN = 64;
static SeenDev   s_devs[MAX_SEEN];
static int       s_nDevs = 0;
static SemaphoreHandle_t s_lock = nullptr;

// Basic advertisement type classifier (matches T-Embed DevType enum: 0=unknown,1=phone,2=audio,4=watch,6=tracker)
static uint8_t classifyAdv(const uint8_t* p, size_t len) {
  size_t i = 0;
  while (i + 1 < len) {
    uint8_t fl = p[i];
    if (!fl || i + fl >= len) break;
    uint8_t ft = p[i + 1];
    const uint8_t* fd = p + i + 2;
    size_t dlen = fl - 1;
    if (ft == 0xFF && dlen >= 2) {
      uint16_t cid = fd[0] | (fd[1] << 8);
      if (cid == 0x004C && dlen >= 3) {
        if (fd[2] == 0x12 || fd[2] == 0x07) return fd[2] == 0x12 ? 6 : 2; // AirTag/FindMy or AirPods
        return 1; // Apple phone/watch
      }
      if (cid == 0x0075 || cid == 0x00E0) return 1; // Samsung / Google
    } else if ((ft == 0x02 || ft == 0x03 || ft == 0x16) && dlen >= 2) {
      uint16_t u = fd[0] | (fd[1] << 8);
      if (u == 0xFEED || u == 0xFD5A || u == 0xFEAA) return 6; // Tile / SmartTag / Tracker
      if (u == 0xFE2C || u == 0x180D) return 2;
    }
    i += fl + 1;
  }
  return 0;
}

class EarScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    int rssi = d->getRSSI();
    if (rssi >= 0 || rssi < -105) return;
    const uint8_t* mac = d->getAddress().getVal();
    const std::vector<uint8_t>& pl = d->getPayload();
    uint8_t type = classifyAdv(pl.data(), pl.size());

    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(10)) != pdTRUE) return;
    uint32_t now = millis();
    int idx = -1;
    for (int i = 0; i < s_nDevs; i++) {
      if (memcmp(s_devs[i].mac, mac, 6) == 0) { idx = i; break; }
    }
    if (idx >= 0) {
      s_devs[idx].rssiS = s_devs[idx].rssiS * 0.7f + (float)rssi * 0.3f;
      if (type) s_devs[idx].type = type;
      s_devs[idx].seenMs = now;
    } else if (s_nDevs < MAX_SEEN) {
      SeenDev& nd = s_devs[s_nDevs++];
      memcpy(nd.mac, mac, 6);
      nd.rssiS = (float)rssi;
      nd.type = type;
      nd.seenMs = now;
    }
    xSemaphoreGive(s_lock);
  }
};
static EarScanCB s_scanCb;

// ---------- ESP-NOW Link State ----------
static volatile uint32_t s_ackMs = 0;
static volatile uint16_t s_matched = 0;
static volatile uint32_t s_ledOffMs = 0;
static uint32_t s_seq = 0;
static uint32_t s_txMs = 0;
static uint32_t s_drawMs = 0;
static bool     s_screenOn = true;

#if ESP_IDF_VERSION_MAJOR >= 5
static void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
#else
static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
#endif
  if (!data || len < (int)sizeof(AckPacket)) return;
  if (memcmp(data, "TGNA", 4) != 0) return;
  const AckPacket* ack = (const AckPacket*)data;
  s_ackMs = millis();
  s_matched = ack->matched;
  digitalWrite(LED_PIN, LOW); // brief blink on ACK
  s_ledOffMs = s_ackMs + 15;
}

// ---------- Display (240x135 TENGEN HUD) ----------
static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite spr = TFT_eSprite(&tft);

static void sendPacket(uint32_t now) {
  EarPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  memcpy(pkt.magic, "TGN1", 4);
  pkt.nodeId = 1;
  bool usb = false;
  pkt.batPct = axpBatPct(usb);
  pkt.flags = usb ? 1 : 0;
  pkt.seq = ++s_seq;

  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(15)) == pdTRUE) {
    // Prune stale (> 10 s)
    int w = 0;
    for (int i = 0; i < s_nDevs; i++) {
      if (now - s_devs[i].seenMs < 10000) s_devs[w++] = s_devs[i];
    }
    s_nDevs = w;
    std::sort(s_devs, s_devs + s_nDevs, [](const SeenDev& a, const SeenDev& b) { return a.rssiS > b.rssiS; });
    int n = min(s_nDevs, 28);
    pkt.count = (uint8_t)n;
    for (int i = 0; i < n; i++) {
      memcpy(pkt.entries[i].mac, s_devs[i].mac, 6);
      pkt.entries[i].rssi = (int8_t)constrain((int)roundf(s_devs[i].rssiS), -110, -20);
      pkt.entries[i].type = s_devs[i].type;
    }
    xSemaphoreGive(s_lock);
  }

  uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  size_t len = 12 + pkt.count * sizeof(EarEntry);
  esp_now_send(bcast, (const uint8_t*)&pkt, len);
}

static void drawHud(uint32_t now) {
  if (!s_screenOn) return;
  const uint16_t bg = tft.color565(5, 8, 13);
  const uint16_t acc = tft.color565(62, 230, 255);
  const uint16_t hot = tft.color565(235, 250, 255);
  const uint16_t grn = tft.color565(105, 255, 110);
  const uint16_t org = tft.color565(255, 140, 26);
  const uint16_t dim = tft.color565(90, 112, 128);
  const uint16_t line = tft.color565(32, 50, 64);

  spr.fillSprite(bg);
  bool linked = s_ackMs && (now - s_ackMs < 3500);
  bool usb = false;
  uint8_t bat = axpBatPct(usb);

  // Header
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(acc, bg);
  spr.drawString("// TENGEN EAR #2", 8, 5, 2);
  char bStr[24];
  snprintf(bStr, sizeof(bStr), "%s%u%%", usb ? "CHG " : "BAT ", (unsigned)bat);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(bat <= 20 ? org : hot, bg);
  spr.drawString(bStr, 232, 5, 2);
  spr.drawFastHLine(8, 23, 224, acc);

  // Left: live mini-radar
  const int rcx = 54, rcy = 76, rr = 38;
  spr.drawCircle(rcx, rcy, rr, line);
  spr.drawCircle(rcx, rcy, rr * 2 / 3, line);
  spr.drawCircle(rcx, rcy, rr / 3, line);
  float sw = (now % 2400) / 2400.0f * 6.2831853f;
  spr.drawLine(rcx, rcy, rcx + (int)(cosf(sw) * rr), rcy + (int)(sinf(sw) * rr), linked ? grn : acc);
  for (int i = 0; i < min(s_nDevs, 14); i++) {
    uint32_t h = (s_devs[i].mac[0] * 31u + s_devs[i].mac[1] * 17u + s_devs[i].mac[2]) % 360u;
    float a = h * (6.2831853f / 360.0f);
    float norm = constrain((-s_devs[i].rssiS - 35.0f) / 60.0f, 0.15f, 0.95f);
    int px = rcx + (int)(cosf(a) * rr * norm);
    int py = rcy + (int)(sinf(a) * rr * norm);
    spr.fillCircle(px, py, 2, s_devs[i].type == 6 ? org : hot);
  }
  spr.fillCircle(rcx, rcy, 3, acc);

  // Right: telemetry & link status
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(linked ? grn : org, bg);
  spr.drawString(linked ? "LINK OK · CH 6" : "SEARCHING #1...", 106, 30, 2);

  spr.setTextColor(dim, bg);
  spr.drawString("BLE HEARD", 106, 52, 1);
  char nStr[16];
  snprintf(nStr, sizeof(nStr), "%d", s_nDevs);
  spr.setTextColor(hot, bg);
  spr.drawString(nStr, 106, 63, 4);

  spr.setTextColor(dim, bg);
  spr.drawString("2X LOCK", 176, 52, 1);
  snprintf(nStr, sizeof(nStr), "%u", (unsigned)(linked ? s_matched : 0));
  spr.setTextColor(linked ? grn : dim, bg);
  spr.drawString(nStr, 176, 63, 4);

  char seqStr[36];
  snprintf(seqStr, sizeof(seqStr), "TX #%lu · [M5] SCREEN", (unsigned long)s_seq);
  spr.setTextColor(dim, bg);
  spr.drawString(seqStr, 106, 102, 1);

  spr.drawFastHLine(8, 119, 224, line);
  spr.setTextColor(dim, bg);
  spr.drawString(linked ? "TENGEN LOCATOR 2X ACTIVE" : "OPEN LOCATOR ON T-EMBED / PC", 8, 123, 1);

  spr.pushSprite(0, 0);
}

void setup() {
  pinMode(BTN_A_PIN, INPUT);
  pinMode(BTN_B_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // OFF

  axpBegin();
  s_lock = xSemaphoreCreateMutex();

  tft.init();
  tft.setRotation(3); // Landscape 240x135 (USB-C on right/left)
  tft.fillScreen(TFT_BLACK);
  spr.setColorDepth(16);
  spr.createSprite(240, 135);

  // Lock Wi-Fi STA to Channel 6 for ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(TENGEN_CH, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onEspNowRecv);
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  // Lightweight passive BLE scanner sharing antenna time with ESP-NOW
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&s_scanCb, true);
  scan->setActiveScan(false);
  scan->setInterval(80);
  scan->setWindow(45);
  scan->setMaxResults(0);
  scan->start(0, false);
}

void loop() {
  uint32_t now = millis();
  if (s_ledOffMs && now >= s_ledOffMs) {
    digitalWrite(LED_PIN, HIGH);
    s_ledOffMs = 0;
  }

  // Front Button [M5]: toggle screen backlight
  static bool prevA = true;
  bool curA = digitalRead(BTN_A_PIN);
  if (!curA && prevA) {
    s_screenOn = !s_screenOn;
    axpScreenPower(s_screenOn);
  }
  prevA = curA;

  // Side Button [B]: immediate ping
  static bool prevB = true;
  bool curB = digitalRead(BTN_B_PIN);
  if (!curB && prevB) {
    sendPacket(now);
  }
  prevB = curB;

  if (now - s_txMs >= 600) {
    s_txMs = now;
    sendPacket(now);
  }
  if (now - s_drawMs >= 120) {
    s_drawMs = now;
    drawHud(now);
  }
  delay(10);
}
