// ============================================================
//  ir.h — IR section: "whose remote is this?"
//
//  Receive only. The board's IR receiver (GPIO1) is read by the RMT peripheral,
//  which measures flash and gap durations itself; we guess the protocol
//  (NEC, Samsung, Sony, RC5, RC6, LG, JVC, Panasonic/Kaseikyo) and, from the
//  address, the likely brand. Unknown signals are shown as a raw burst.
//  The IR LED (GPIO2) is never touched: the stick transmits nothing.
//
//  ir_decode.cpp — parsing (pure code, also built in the simulator),
//  ir.cpp        — hardware (RMT), menu section, JSON for the tablet,
//  ir_screens.cpp — screen.
// ============================================================
#pragma once
#include <Arduino.h>
#include "core/section.h"

#define IR_RAW_MAX  128               // durations in the last burst (µs; flash, gap, ...)
#define IR_HIST     8

struct IrCode {
  char     proto[12];                 // "NEC", "NEC-EXT", "SAMSUNG", "SONY12", "RC5", "RC6", "LG", "JVC", "KASEIKYO", "RAW"
  uint32_t addr, cmd;
  uint8_t  bits;
  bool     rep;                       // button held (repeat)
  uint8_t  toggle;                    // RC5/RC6: "new press" bit
  const char* brand;                  // "probably whose" (string from the table, never nullptr)
  uint32_t t;                         // millis()
};

struct IrModel {
  bool     ok = false;                // receiver started
  bool     on = false;                // listening now
  uint32_t frames = 0;                // bursts total
  uint32_t gen = 0;                   // grows with each parsed burst (for the tablet)
  IrCode   last = {};
  bool     have = false;
  IrCode   hist[IR_HIST];             // [0] = newest (repeats don't pile up)
  int      histN = 0;
  uint16_t raw[IR_RAW_MAX];           // last burst: flash, gap, flash...
  int      rawN = 0;
  uint32_t presses = 0;               // presses (without repeats)
};
extern IrModel g_ir;

// parse a duration burst (µs, starting with a flash) and put it in the model
bool irFeed(const uint16_t* d, int n, uint32_t now);
// parse without the model: true = recognized something (RAW included)
bool irDecode(const uint16_t* d, int n, IrCode& out);
const char* irBrand(const IrCode& c);

void irBegin();                       // start background IR receiver at boot (for emergency IR trigger)
bool irStart();                       // enter IR inspect mode (section open or tablet watching)
void irStop();                        // leave IR inspect mode (keeps background receiver running)
void irTick(uint32_t now);
void irPollBg(uint32_t now);          // background poll from main loop()
void irEvilSetLearn(bool on);         // true while Settings -> Emergency is open (binds next IR button)
void irEvilLabel(char* out, size_t n);// human-readable status of the bound IR trigger button
void irJson(String& o);               // last burst for the tablet (SSE event "ir")

void drawIr(uint32_t now);
void iconRemote(int x, int y, uint16_t c, uint16_t bg);

extern const Section irSection;

// placeholder labels for "brand unknown" (the screen compares against them)
#include "core/lang.h"
#define IR_NEC_GENERIC    L("NEC remote (many brands)", "пульт NEC (много марок)")
#define IR_UNKNOWN_REMOTE L("unknown remote", "неизвестный пульт")
#define IR_UNKNOWN        L("unknown", "неизвестно")
