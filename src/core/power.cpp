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

