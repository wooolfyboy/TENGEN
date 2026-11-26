// ============================================================
//  locator.h — Dedicated 2-stick BLE Triangulation / Stereo Locator mode
//  Receives ESP-NOW frames ("TGN1") from M5StickC Plus Ear #2 on Channel 6,
//  matches BLE MAC addresses with T-Embed's entities, computes 2D room
//  positions (X, Z) + confidence radius, and draws the room radar.
// ============================================================
#pragma once
#include <Arduino.h>
#include "core/section.h"

void locBegin();
bool locActive();
bool locWebUse();
void locWebStop();
void locTick(uint32_t now);
void locJson(String& out);
bool locCalibrate(String& out);
void locSetBase(float meters);
float locGetBase();
int   locGetOffset();

extern const Section locatorSection;
