// ============================================================
//  audio.h — sound: microphone (Sound section) and speaker (UI sounds)
// ============================================================
#pragma once
#include <Arduino.h>
#include "core/section.h"

void audioBegin();                   // speaker (UI sounds)
void audioClick();                   // wheel click
void audioBeep(uint16_t hz, uint16_t ms);
void audioSiren(uint16_t hz, uint16_t ms, int volPct);   // emergency siren (0..100%, ignores UI mute)
void audioGeiger(int pct);           // Geiger counter: the closer (pct 0..100), the faster the clicks

extern const Section soundSection;

// web page (Web section)
bool audioWebUse();                  // start the mic (if allowed in settings)
void audioWebTick(uint32_t now);
void audioWebStop();
void audioJson(String& out);
void audioStreamJson(String& out);   // compact frame for the tablet (SSE stream)

// laptop audio ("PA ..." lines over USB, parsed by pc.cpp)
void audioPcFrame(float levelDb, int domHz, const uint8_t* bands32, const int8_t* wave64,
                  const int8_t* xy64x2, bool stereo);
bool audioPcLive(uint32_t now);      // frames from the laptop are arriving (< 0.6 s ago)
