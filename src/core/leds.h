// ============================================================
//  leds.h — LED strip (8 x WS2812): theme-aware Zen wave & ripples
// ============================================================
#pragma once
#include <Adafruit_NeoPixel.h>

extern Adafruit_NeoPixel strip;
extern int      g_ledEvent;        // 0 none, +1 wave, +2 center ripple (capture/enter), -1 alert fade
extern uint32_t g_ledStart;

void ledsBegin();
void ledsEvent(int dir);           // start event animation (theme-aware, 0 idle drain)
void ledsTick();                   // animation step (every loop pass)
void ledsOff();
