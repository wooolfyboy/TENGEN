// ============================================================
//  power.cpp — power off / on with the side button
// ============================================================
#include <esp_sleep.h>
#include "core/timeutil.h"
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include "core/power.h"
#include "core/board.h"
#include "core/display.h"
#include "core/theme.h"
#include "core/leds.h"
#include "core/settings.h"
#include "core/ui.h"
#include "core/crumb.h"
#include "audio/audio.h"

uint32_t g_powerHold = 0;
static uint32_t s_lastTouch = 0;
static bool     s_dimmed = false;

void powerTouch() {
  s_lastTouch = millis();
  if (g_screenOff) { g_screenOff = false; applyBrightness(); }   // screen was turned off from the tablet: stick input wakes it
  if (s_dimmed) { s_dimmed = false; applyBrightness(); }
}
bool powerIdle() { return s_dimmed; }
void powerIdleTick(uint32_t now) {
  uint32_t limit = DIM_SECS[g_dimIdx] * 1000UL;
  if (!limit) { if (s_dimmed) powerTouch(); return; }  // disabled in settings
  if (!s_dimmed && !g_screenOff && msSince(now, s_lastTouch) > limit) {
    s_dimmed = true;
    analogWrite(TFT_BL, g_scrBright * 255 / 100 / 4);   // quarter brightness
  }
}
bool     g_ignoreUser = false;

void sleepNow() {
  // wait for the button release, otherwise the board wakes right away
  while (digitalRead(PIN_USER_KEY) == LOW) { crumbFeed(); delay(10); }
  delay(50);
  pinMode(TFT_BL, OUTPUT);       digitalWrite(TFT_BL, LOW);
  pinMode(BOARD_PWR_EN, OUTPUT); digitalWrite(BOARD_PWR_EN, LOW);   // cut power to peripherals
  gpio_hold_en((gpio_num_t)TFT_BL);
  gpio_hold_en((gpio_num_t)BOARD_PWR_EN);
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en((gpio_num_t)PIN_USER_KEY);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_USER_KEY);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_KEY, 0);       // wake on press
  esp_deep_sleep_start();
}

void powerOff() {
  settingsSaveNow();
  strip.clear(); strip.show();
  uint32_t t0 = millis();
  for (;;) {
    crumbFeed();
    uint32_t t = millis() - t0;
    if (t > 1450) break;
    shutFrame(t);
    spr.pushSprite(0, 0);                              // no corner brackets: the whole screen fades
  }
  spr.fillSprite(TFT_BLACK); spr.pushSprite(0, 0);
  tft.writecommand(0x28);   // display off
  tft.writecommand(0x10);   // display sleep
  sleepNow();
}

void powerEmergency() {
  crumb("evil mode");
  settingsSaveNow();
  // wake screen to full brightness even if dimmed or turned off from tablet
  g_screenOff = false;
  tft.writecommand(0x29);
  analogWrite(TFT_BL, 255);

  const uint32_t total = (uint32_t)constrain(g_emSec, 1, 30) * 1000UL;
  const int vol = constrain(g_emVol, 0, 100);
  const int ledBr = constrain(g_emLed, 0, 100) * 255 / 100;
  strip.setBrightness(ledBr);

  uint32_t t0 = millis();
  uint32_t lastSiren = 0;
  uint32_t lastLed = 0;
  bool hiTone = true;

  for (;;) {
    crumbFeed();
    uint32_t now = millis();
    uint32_t elapsed = now - t0;
    if (elapsed >= total) break;
    uint32_t rem = total - elapsed;

    // Siren klaxon: alternates 880 / 1380 Hz, accelerates to frantic beeps in final 1.2 s
    uint32_t period = rem < 1200 ? 110 : 240;
    if (vol > 0 && (!lastSiren || now - lastSiren >= period)) {
      lastSiren = now;
      uint16_t hz = (rem < 1200) ? (hiTone ? 2100 : 1650) : (hiTone ? 1380 : 880);
      audioSiren(hz, (uint16_t)(period - 20), vol);
      hiTone = !hiTone;
    }

    // Blazing red strobe + angry chasing pulse on the 8 WS2812 LEDs
    if (now - lastLed >= 35) {
      lastLed = now;
      strip.clear();
      if (ledBr > 0) {
        bool strobe = ((elapsed / (rem < 1200 ? 55 : 110)) & 1) == 0;
        if (strobe) {
          for (int i = 0; i < WS2812_N; i++) strip.setPixelColor(i, strip.Color(255, 0, 0));
        } else {
          int head = (elapsed / 45) % WS2812_N;
          strip.setPixelColor(head, strip.Color(255, 25, 0));
          strip.setPixelColor((head + WS2812_N - 1) % WS2812_N, strip.Color(120, 0, 0));
        }
      }
      strip.show();
    }

    evilFrame(elapsed, total);
    spr.pushSprite(0, 0);
    delay(16);
  }

  // Final power-down drop
  if (vol > 0) audioSiren(180, 320, vol);
  if (ledBr > 0) {
    for (int i = 0; i < WS2812_N; i++) strip.setPixelColor(i, strip.Color(255, 80, 80));
    strip.show();
  }
  spr.fillSprite(C(220, 0, 0));
  spr.pushSprite(0, 0);
  delay(180);
  strip.clear(); strip.show();
  spr.fillSprite(TFT_BLACK); spr.pushSprite(0, 0);
  delay(140);
  tft.writecommand(0x28);
  tft.writecommand(0x10);
  sleepNow();
}

// Called at the very start of setup(): if the button woke us, require a
// PWR_ON_MS hold, otherwise (accidental touch) go back to sleep.
void checkWakeHold() {
  gpio_hold_dis((gpio_num_t)TFT_BL);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, LOW);   // backlight off until the screen is cleared
  gpio_hold_dis((gpio_num_t)BOARD_PWR_EN);
  gpio_deep_sleep_hold_dis();
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) return;   // normal power-on
  rtc_gpio_deinit((gpio_num_t)PIN_USER_KEY);
  pinMode(PIN_USER_KEY, INPUT_PULLUP);
  uint32_t t0 = millis();
  while (digitalRead(PIN_USER_KEY) == LOW && millis() - t0 < PWR_ON_MS) delay(10);
  if (millis() - t0 < PWR_ON_MS) sleepNow();     // released too early: don't boot
  g_ignoreUser = true;                           // button still held: don't treat as a press
}

