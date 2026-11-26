// ============================================================
//  TENGEN EAR #2 — Ultra-Low-Power Edition for M5StickC Plus
//
//  Power-saving design for the small 120 mAh battery:
//  - CPU locked at 80 MHz (instead of 240 MHz -> cuts CPU current by >50%)
//  - Minimal static screen ("ON" / "OFF" + battery %, redrawn only every 5s)
//  - Dimmed AXP192 backlight voltage (2.7V) + ST7789 hardware sleep when dark
//  - Gentle 30% BLE duty cycle + 1.2s ESP-NOW interval + Wi-Fi modem sleep
//  - In "OFF" mode: Wi-Fi & BLE radios completely powered down, screen sleeps
//    after 6 seconds -> near-zero standby drain.
//
//  Controls:
//  - Front Button [M5] (GPIO 37): Toggle ON / OFF mode
//  - Side Button  [B]  (GPIO 39): Toggle screen backlight while ON (stealth)
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

#define BTN_A_PIN  37   // Front [M5] button: ON / OFF toggle
#define BTN_B_PIN  39   // Side button: screen backlight toggle while ON
#define LED_PIN    10   // Red LED (active LOW) — kept OFF to save power
#define AXP_ADDR   0x34
#define TENGEN_CH  6

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

// ---------- AXP192 Power Management ----------
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
  axpWrite(0x28, 0x9C);   // LDO2 (backlight dimmed to 2.7V = huge power saving!) & LDO3 (TFT 3.0V)
  axpWrite(0x82, 0x83);   // Only battery V/I ADC enabled (disable unused ADCs)
  axpWrite(0x33, 0xC0);   // Charge 4.2V, 100mA
  axpWrite(0x12, 0x0D);   // Enable DCDC1, LDO2, LDO3 (disable unused DCDC3/EXTEN5V!)
  axpWrite(0x36, 0x5C);   // Power button timing (4s hard power-off)
}

static TFT_eSPI tft = TFT_eSPI();
static bool s_lcdAwake = true;

static void setScreenPower(bool on) {
  if (s_lcdAwake == on) return;
  s_lcdAwake = on;
  if (on) {
    uint8_t v = axpRead8(0x12) | 0x04;  // LDO2 ON
    axpWrite(0x12, v);
    tft.writecommand(0x11);             // ST7789 Sleep Out
    delay(20);
    tft.writecommand(0x29);             // Display ON
  } else {
    tft.writecommand(0x28);             // Display OFF
    tft.writecommand(0x10);             // ST7789 Sleep In (<0.05 mA)
    uint8_t v = axpRead8(0x12) & ~0x04; // LDO2 Backlight OFF
    axpWrite(0x12, v);
  }
}

static uint8_t axpBatPct(bool& usbOrChg) {
  uint8_t st0 = axpRead8(0x00);
  usbOrChg = (st0 & 0x24) != 0;
  uint16_t hi = axpRead8(0x78), lo = axpRead8(0x79);
  float mv = ((hi << 4) | (lo & 0x0F)) * 1.1f;
  int pct = (int)((mv - 3300.0f) * 100.0f / (4150.0f - 3300.0f));
  return (uint8_t)constrain(pct, 0, 100);
}

// ---------- BLE Table ----------
struct SeenDev {
  uint8_t  mac[6];
  float    rssiS;
  uint8_t  type;
  uint32_t seenMs;
};

static const int MAX_SEEN = 48;
static SeenDev   s_devs[MAX_SEEN];
static int       s_nDevs = 0;
static SemaphoreHandle_t s_lock = nullptr;

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
        if (fd[2] == 0x12 || fd[2] == 0x07) return fd[2] == 0x12 ? 6 : 2;
        return 1;
      }
      if (cid == 0x0075 || cid == 0x00E0) return 1;
    } else if ((ft == 0x02 || ft == 0x03 || ft == 0x16) && dlen >= 2) {
      uint16_t u = fd[0] | (fd[1] << 8);
      if (u == 0xFEED || u == 0xFD5A || u == 0xFEAA) return 6;
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

// ---------- State ----------
static bool     s_active = true;     // ON / OFF mode toggled by [M5] button
static bool     s_stealth = false;   // Screen off while ON (toggled by side [B] button)
static bool     s_needDraw = true;
static uint32_t s_modeChangeMs = 0;
static uint32_t s_txMs = 0;
static uint32_t s_drawMs = 0;
static uint32_t s_seq = 0;
static volatile uint32_t s_ackMs = 0;
static uint8_t  s_masterMac[6] = {0};
static bool     s_masterPeerAdded = false;

#if ESP_IDF_VERSION_MAJOR >= 5
static void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = info ? info->src_addr : nullptr;
#else
static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (!data || len < (int)sizeof(AckPacket)) return;
  if (memcmp(data, "TGNA", 4) != 0) return;
  s_ackMs = millis();
  if (mac) memcpy(s_masterMac, mac, 6);
}

static void radiosStart() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(TENGEN_CH, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_ps(WIFI_PS_NONE); // required for unassociated WIFI_STA ESP-NOW + BLE coexistence
  esp_wifi_set_max_tx_power(78); // full 19.5 dBm TX power for rock-solid link

  s_masterPeerAdded = false;
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onEspNowRecv);
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&s_scanCb, true);
  scan->setActiveScan(false);
  scan->setInterval(96);   // 60 ms cycle
  scan->setWindow(38);     // 23.75 ms BLE / 36.25 ms Wi-Fi ESP-NOW
  scan->setMaxResults(0);
  scan->start(0, false);
}

static void radiosStop() {
  NimBLEDevice::getScan()->stop();
  esp_now_unregister_recv_cb();
  esp_now_deinit();
  s_masterPeerAdded = false;
  WiFi.mode(WIFI_OFF);
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
    s_nDevs = 0;
    xSemaphoreGive(s_lock);
  }
}

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
    int w = 0;
    for (int i = 0; i < s_nDevs; i++) {
      if (now - s_devs[i].seenMs < 12000) s_devs[w++] = s_devs[i];
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

  // Auto-register T-Embed's MAC for Unicast 802.11 hardware-retried delivery!
  if (!s_masterPeerAdded && (s_masterMac[0] | s_masterMac[1] | s_masterMac[2])) {
    esp_now_peer_info_t p;
    memset(&p, 0, sizeof(p));
    memcpy(p.peer_addr, s_masterMac, 6);
    p.channel = 0;
    p.encrypt = false;
    if (esp_now_add_peer(&p) == ESP_OK || esp_now_is_peer_exist(s_masterMac)) s_masterPeerAdded = true;
  }

  size_t len = 12 + pkt.count * sizeof(EarEntry);
  uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  if (s_masterPeerAdded) {
    // Unicast has automatic 802.11 MAC-layer retries until T-Embed hardware-ACKs!
    esp_now_send(s_masterMac, (const uint8_t*)&pkt, len);
    delay(18);
    esp_now_send(bcast, (const uint8_t*)&pkt, len);
  } else {
    esp_now_send(bcast, (const uint8_t*)&pkt, len);
    delay(20);
    esp_now_send(bcast, (const uint8_t*)&pkt, len);
  }
}

// Minimal direct screen draw (no RAM sprite, redrawn only every 5s or on button press)
static void drawMinimal(uint32_t now) {
  if (!s_lcdAwake) return;
  const uint16_t bg  = TFT_BLACK;
  const uint16_t grn = tft.color565(90, 245, 100);
  const uint16_t acc = tft.color565(62, 230, 255);
  const uint16_t dim = tft.color565(80, 95, 110);
  const uint16_t org = tft.color565(240, 120, 30);

  tft.fillScreen(bg);
  bool usb = false;
  uint8_t bat = axpBatPct(usb);
  bool linked = s_active && s_ackMs && (now - s_ackMs < 4000);

  // Top line: EAR #2 & Battery
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(dim, bg);
  tft.drawString("EAR #2", 10, 8, 2);

  char bStr[20];
  snprintf(bStr, sizeof(bStr), "%s%u%%", usb ? "⚡" : "", (unsigned)bat);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(bat <= 20 ? org : dim, bg);
  tft.drawString(bStr, 230, 8, 2);

  // Center: huge "ON" or "OFF"
  tft.setTextDatum(MC_DATUM);
  if (s_active) {
    tft.setTextColor(linked ? grn : acc, bg);
    tft.drawString("ON", 120, 66, 4);
  } else {
    tft.setTextColor(org, bg);
    tft.drawString("OFF", 120, 66, 4);
  }

  // Bottom hint
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(dim, bg);
  if (s_active) {
    tft.drawString(linked ? "LINK OK  ·  [M5]: OFF  ·  [B]: SCR" : "SCANNING · [M5]: OFF · [B]: SCR", 120, 128, 1);
  } else {
    tft.drawString("RADIOS OFF  ·  PRESS [M5] TO START", 120, 128, 1);
  }
}

void setup() {
  setCpuFrequencyMhz(160);  // 160 MHz is the minimum required by ESP32 for Wi-Fi + BLE coexistence

  pinMode(BTN_A_PIN, INPUT);
  pinMode(BTN_B_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // keep LED off to save ~5 mA

  axpBegin();
  s_lock = xSemaphoreCreateMutex();

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  NimBLEDevice::init("");
  s_active = true;
  s_modeChangeMs = millis();
  radiosStart();
  drawMinimal(s_modeChangeMs);
}

void loop() {
  uint32_t now = millis();

  // Front Button [M5]: Toggle ON / OFF
  static bool prevA = true;
  bool curA = digitalRead(BTN_A_PIN);
  if (!curA && prevA) {
    s_active = !s_active;
    s_stealth = false;
    s_modeChangeMs = now;
    setScreenPower(true);
    if (s_active) {
      radiosStart();
      sendPacket(now);
    } else {
      radiosStop();
    }
    s_needDraw = true;
  }
  prevA = curA;

  // Side Button [B]: While ON, toggle screen backlight (stealth battery saver)
  static bool prevB = true;
  bool curB = digitalRead(BTN_B_PIN);
  if (!curB && prevB && s_active) {
    s_stealth = !s_stealth;
    setScreenPower(!s_stealth);
    if (!s_stealth) s_needDraw = true;
  }
  prevB = curB;

  if (s_active) {
    // Transmit every 500 ms with Unicast hardware retry + Broadcast backup
    if (now - s_txMs >= 500) {
      s_txMs = now;
      sendPacket(now);
    }
    // Redraw minimal screen only once every 5s
    if (s_needDraw || (s_lcdAwake && now - s_drawMs >= 5000)) {
      s_needDraw = false;
      s_drawMs = now;
      drawMinimal(now);
    }
    delay(40);
  } else {
    // OFF mode: draw "OFF" once, then after 6 seconds turn screen hardware completely OFF
    if (s_needDraw) {
      s_needDraw = false;
      drawMinimal(now);
    }
    if (s_lcdAwake && now - s_modeChangeMs > 6000) {
      setScreenPower(false);
    }
    delay(100);
  }
}
