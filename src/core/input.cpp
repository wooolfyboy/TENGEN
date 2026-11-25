// ============================================================
//  input.cpp — wheel (interrupt-driven) and buttons
// ============================================================
#include <atomic>
#include <soc/gpio_reg.h>
#include "core/input.h"
#include "core/board.h"
#include "core/menu.h"
#include "core/power.h"
#include "audio/audio.h"

// --- wheel: quadrature decoder in the ISR ---
// Transition table (prev AB << 2 | new) -> -1/0/+1; 4 transitions = one click.
// Bounce gives invalid transitions (0) and cancels itself.
static const DRAM_ATTR int8_t TBL[16] = { 0,-1, 1, 0,  1, 0, 0,-1, -1, 0, 0, 1,  0, 1,-1, 0 };
static volatile uint8_t s_prev = 3;
static volatile int8_t  s_accum = 0;
static std::atomic<int> s_steps{0};

static void IRAM_ATTR encIsr() {
  uint32_t in = REG_READ(GPIO_IN_REG);
  uint8_t cur = (((in >> PIN_ENC_A) & 1) << 1) | ((in >> PIN_ENC_B) & 1);
  if (cur == s_prev) return;
  s_accum += TBL[(s_prev << 2) | cur];
  s_prev = cur;
  if (s_accum >= 4)       { s_steps.fetch_add(1, std::memory_order_relaxed); s_accum = 0; }
  else if (s_accum <= -4) { s_steps.fetch_sub(1, std::memory_order_relaxed); s_accum = 0; }
}

void inputBegin() {
  pinMode(PIN_ENC_KEY,  INPUT_PULLUP);
  pinMode(PIN_ENC_A,    INPUT_PULLUP);
  pinMode(PIN_ENC_B,    INPUT_PULLUP);
  pinMode(PIN_USER_KEY, INPUT_PULLUP);
  s_prev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encIsr, CHANGE);
}

// Side button: short press = back, hold 2 s = power off. Wheel press = select (the section decides what).
void inputPoll() {
  static int lastEnc = HIGH;
  static uint32_t lastEncMs = 0;
  static bool userDown = false;
  static uint32_t userDownMs = 0;
  static bool userWoke = false;          // the press only woke the screen
  int enc  = digitalRead(PIN_ENC_KEY);
  int user = digitalRead(PIN_USER_KEY);
  uint32_t now = millis();

  if (enc == LOW && lastEnc == HIGH && now - lastEncMs > 250) {
    bool wasDim = powerIdle();
    powerTouch();
    if (!wasDim) { navClick(); audioClick(); }   // first press while dimmed only wakes the screen
    lastEncMs = now;
  }
  lastEnc = enc;

  int st = s_steps.exchange(0);
  if (st) { bool wasDim = powerIdle(); powerTouch(); if (wasDim) st = 0; }
  if (st) audioClick();
  for (; st > 0; st--) navRotate(+1);
  for (; st < 0; st++) navRotate(-1);

  if (g_ignoreUser) {                    // after power-on wait for release
    if (user == HIGH) g_ignoreUser = false;
    return;
  }
  if (user == LOW && !userDown) { userDown = true; userDownMs = now; userWoke = powerIdle(); powerTouch(); }
  if (userDown && user == LOW) {
    uint32_t held = now - userDownMs;
    g_powerHold = held >= PWR_SHOW_MS ? held : 0;
    if (held >= PWR_OFF_MS) powerOff();
  }
  if (userDown && user == HIGH) {
    uint32_t held = now - userDownMs;
    userDown = false;
    g_powerHold = 0;
    if (held >= 30 && held < PWR_SHOW_MS && !userWoke) navBack();   // short press (30 ms debounce)
  }
}
