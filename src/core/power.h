// ============================================================
//  power.h — power off / on with the side button
//
//  "Off" = ESP32 deep sleep: screen, LEDs and peripherals are
//  unpowered; ONLY holding the side button wakes it.
// ============================================================
#pragma once
#include <Arduino.h>

static const uint32_t PWR_SHOW_MS = 600;    // show the gauge after this many ms of holding
static const uint32_t PWR_OFF_MS  = 2000;   // hold this long to power off
static const uint32_t PWR_ON_MS   = 1000;   // hold this long to power on

extern uint32_t g_powerHold;                // how long it has been held (for the on-screen gauge)
extern bool     g_ignoreUser;               // ignore the button until released

void checkWakeHold();                       // at the very start of setup()

// Power saving (Settings -> Auto dim, off by default): after the set idle
// time the screen dims to 25 % and frames get less frequent.
void powerTouch();                          // a press / wheel turn happened
bool powerIdle();                           // screen is dimmed now
void powerIdleTick(uint32_t now);
void powerOff();
void powerEmergency();              // Evil Mode: red strobe, loud siren, countdown -> deep sleep
void drawPowerHold();
void shutFrame(uint32_t t);         // power-off animation frame at t ms (0..1400)
void evilFrame(uint32_t elapsed, uint32_t total);
