// ============================================================
//  serout.cpp — USB write queue (see serout.h).
//  64 KB ring buffer in PSRAM: lines are stored whole, sent in chunks as the port has room.
// ============================================================
#include <Arduino.h>
#include <string.h>
#include "core/psram.h"
#include "link/serout.h"

static const size_t CAP = 64 * 1024;      // max pending bytes
static const size_t DROP_AT = 12 * 1024;  // droppable lines are refused above this queue size
static char*  s_buf = nullptr;
static size_t s_head = 0, s_len = 0;      // ring start and data length
static bool   s_open = false;             // a line is being built (begin..end)

static bool ensure() { if (!s_buf) s_buf = psAlloc<char>(CAP); return s_buf != nullptr; }
static void put(const char* s, size_t n) {
  if (n > CAP - s_len) n = CAP - s_len;      // never overflow the ring buffer
  if (!n) return;
  size_t tail = (s_head + s_len) % CAP;
  size_t a = n < CAP - tail ? n : CAP - tail;
  memcpy(s_buf + tail, s, a);
  if (n > a) memcpy(s_buf, s + a, n - a);
  s_len += n;
}

bool serOutBegin(size_t total, bool droppable) {
  if (!ensure() || s_open) return false;
  if (droppable && s_len > DROP_AT) return false;
  if (s_len + total + 1 > CAP) return false;
  s_open = true; return true;
}
void serOutPart(const char* s, size_t n) { if (s_open) put(s, n); }
void serOutEnd() { if (!s_open) return; put("\n", 1); s_open = false; }

bool serOutLine(const char* s, size_t n, bool droppable) {
  if (!serOutBegin(n, droppable)) return false;
  serOutPart(s, n); serOutEnd(); return true;
}
bool serOutLine(const char* s) { return serOutLine(s, strlen(s), false); }
size_t serOutQueued() { return s_len; }

void serOutPump() {
  if (!s_len || s_open) return;
  for (int guard = 0; guard < 8 && s_len; guard++) {
    int room = Serial.availableForWrite();
    if (room <= 0) return;                 // PC is not keeping up, retry later
    size_t n = s_len < (size_t)room ? s_len : (size_t)room;
    if (n > CAP - s_head) n = CAP - s_head;     // up to ring end; the rest goes next time
    size_t w = Serial.write((const uint8_t*)(s_buf + s_head), n);
    if (!w) return;
    s_head = (s_head + w) % CAP; s_len -= w;
  }
}
