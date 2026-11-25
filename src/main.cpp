// ============================================================
//  TENGEN — radio spectrum analyzer for the LilyGO T-Embed CC1101
//  Receive-only: Bluetooth, Wi-Fi (2.4 GHz), Sub-GHz (CC1101).
//
//  core/ UI, menu, input, power, battery, settings, LEDs
//  ble/  Bluetooth      wifi/ Wi-Fi      subghz/ CC1101 driver + screens
// ============================================================
#include "core/crumb.h"
#include <Arduino.h>
#include "core/board.h"
#include "core/display.h"
#include "core/input.h"
#include "core/menu.h"
#include "core/power.h"
#include "core/battery.h"
#include "core/settings.h"
#include "core/leds.h"
#include "ble/ble.h"
#include "core/boot.h"
#include "pc/pc.h"
#include "web/web.h"
#include "gallery/sat.h"
#include <driver/gpio.h>
#include <esp_ota_ops.h>
#include "audio/audio.h"
#include "core/perf.h"


// Earliest hook before setup(): backlight off so uninitialised screen garbage is not visible.
extern "C" void initVariant() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
}
// Even earlier: a global constructor runs before any Arduino init.
__attribute__((constructor(101))) static void earlyBacklightOff() {
  gpio_set_direction((gpio_num_t)TFT_BL, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)TFT_BL, 0);
}

// OTA rollback: a firmware updated over Wi-Fi is "pending verify". We confirm it after 15 s
// of uptime; if it hangs or reboots earlier, the bootloader restores the old one.
extern "C" bool verifyRollbackLater() { return true; }
static void confirmFirmware(uint32_t now) {
  static bool done = false;
  if (done || now < 15000) return;
  done = true;
  esp_ota_mark_app_valid_cancel_rollback();      // no-op after a USB flash
}

void setup() {
  crumbBoot();          // black box: previous reset reason and last step
  checkWakeHold();      // after "power off", wake only by holding the side button

  // shared SPI bus: deselect every device until it is used
  pinMode(BOARD_PWR_EN, OUTPUT); digitalWrite(BOARD_PWR_EN, HIGH);
  pinMode(PIN_TFT_CS,  OUTPUT);  digitalWrite(PIN_TFT_CS,  HIGH);
  pinMode(PIN_SD_CS,   OUTPUT);  digitalWrite(PIN_SD_CS,   HIGH);
  pinMode(PIN_LORA_CS, OUTPUT);  digitalWrite(PIN_LORA_CS, HIGH);
  pinMode(PIN_NRF_CS,  OUTPUT);  digitalWrite(PIN_NRF_CS,  HIGH);

  Serial.setRxBufferSize(2048);
  Serial.setTxBufferSize(8192);    // PC app stream (link/serout), non-blocking
  Serial.begin(115200);  // USB: the laptop program writes here (PC section)
  inputBegin();
  displayBegin();
  ledsBegin();
  settingsBegin();
  batBegin();
  bootScreen();          // boot animation
  bleBegin();
  audioBegin();          // speaker: UI sounds
  satBegin();            // saved satellite orbits (Gallery > Globe)
  navBegin();
  navDraw(millis());
  crumbWatchdogStart();                // from here a hang > 15 s = reboot with the step logged
}

void loop() {
  static uint32_t lastDraw = 0;
  uint32_t tLoop = micros();
  crumbFeed();
  crumb("input");   inputPoll();
  uint32_t now = millis();
  crumb("ble sample"); bleSample(now);
  ledsTick();
  batTick(now);
  crumb("pc poll"); pcPoll(now);                        // lines from the laptop over USB (PC)
  crumb("usb out"); webLinkPump();                     // to USB: web page and PC app stream
  crumb("nav tick"); navTick(now);                       // work of the open section (scans, sweeps)

  powerIdleTick(now);
  confirmFirmware(now);
  // dimmed mode: 4x fewer frames to spare CPU and bus; screen off: skip rendering completely
  uint32_t frame = (powerIdle() || g_screenOff) ? REDRAW_MS * 4 : (g_fastFrame ? 33 : REDRAW_MS);
  if (now - lastDraw >= frame) {
    lastDraw = now;
    crumb("ble update"); bleUpdate(now);                   // always needed: menu shows device count, LED strip shows events
    g_fastFrame = false;              // the screen asks again if needed
    if (g_powerHold) {
      uint32_t tDraw = micros();
      drawPowerHold();
      perfDraw(micros() - tDraw);
    } else if (!g_screenOff) {
      uint32_t tDraw = micros();
      crumb("draw"); navDraw(now);
      perfDraw(micros() - tDraw);
    }
    crumb("autosave"); settingsAutosave(now);
  }
  perfLoop(micros() - tLoop);
  perfTick(now);
  delay((powerIdle() || g_screenOff) && !g_powerHold ? 12 : 5);
}
