// ============================================================
//  leds.cpp — LED strip: theme-aware Zen waves & center ripples
// ============================================================
#include "core/leds.h"
#include "core/board.h"
#include "core/settings.h"

Adafruit_NeoPixel strip(WS2812_N, PIN_WS2812, NEO_GRB + NEO_KHZ800);
int      g_ledEvent = 0;
uint32_t g_ledStart = 0;

void ledsBegin() { strip.begin(); strip.clear(); strip.show(); }
void ledsOff()   { strip.clear(); strip.show(); }
void ledsEvent(int dir) {
  if (g_ledBright <= 0) return;
  g_ledEvent = dir;
  g_ledStart = millis();
}

static uint32_t themeAccent(int b, bool crest) {
  if (g_theme == THEME_KURENAI) {
    return crest ? strip.Color(b, b * 3 / 5, b / 2) : strip.Color(b, b / 10, b / 8);
  }
  return crest ? strip.Color(b * 3 / 4, b, b) : strip.Color(0, b * 4 / 5, b);
}

void ledsTick() {
  if (g_ledEvent == 0) return;
  if (g_ledBright <= 0) { strip.clear(); strip.show(); g_ledEvent = 0; return; }
  uint32_t now = millis();
  static uint32_t s_lastLedMs = 0;
  uint32_t dur = (g_ledEvent == 2) ? 480 : 1100;
  uint32_t t = now - g_ledStart;
  if (t >= dur) { strip.clear(); strip.show(); g_ledEvent = 0; return; }
  if (now - s_lastLedMs < 28) return;           // ~35 Hz smooth RMT updates
  s_lastLedMs = now;
  float p = (float)t / dur;                     // 0..1
  strip.clear();
  if (g_ledEvent == 2) {
    // Concentric ripple expanding from the center Tengen pair (3.5 -> 0 & 7)
    float radius = p * 4.5f;
    for (int i = 0; i < WS2812_N; i++) {
      float dist = fabsf((float)i - 3.5f);
      float w = 1.0f - fabsf(dist - radius) * 0.85f;
      if (w > 0.0f) {
        int b = (int)(220.0f * w * (1.0f - p * 0.65f));
        if (b > 0) strip.setPixelColor(i, themeAccent(b, w > 0.78f));
      }
    }
  } else if (g_ledEvent > 0) {
    // Rising ether wave bottom-to-top in active theme palette
    float head = p * (WS2812_N + 3.5f);
    for (int i = 0; i < WS2812_N; i++) {
      float d = head - i;
      if (d >= 0.0f && d < 4.0f) {
        int b = (int)(225.0f * (1.0f - d / 4.0f) * (1.0f - p * 0.25f));
        if (b > 0) strip.setPixelColor(i, themeAccent(b, d < 0.9f));
      }
    }
  } else {
    // Alert / departure pulse fading out
    int b = (int)(220.0f * (1.0f - p) * (1.0f - p));
    uint32_t c = (g_theme == THEME_KURENAI) ? strip.Color(b, 0, b / 12) : strip.Color(b, b * 2 / 5, 0);
    for (int i = 0; i < WS2812_N; i++) strip.setPixelColor(i, c);
  }
  strip.show();
}
