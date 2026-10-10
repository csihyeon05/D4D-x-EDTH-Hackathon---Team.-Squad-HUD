#include "pulse_trace.h"
#include "core/log.h"
#include <atomic>
#include <stdio.h>
#ifndef CFG_PULSE_TRACE
#define CFG_PULSE_TRACE 0
#endif
namespace PulseTrace {
#if CFG_PULSE_TRACE
namespace {
constexpr uint16_t CAPACITY = 2048;
uint16_t samples[CAPACITY];
uint32_t times[CAPACITY];
uint16_t count = 0, next = 0, printed = 0, oldest = 0;
uint32_t lastPrint = 0;
bool hadRate = false;
std::atomic<bool> ready{false};
}
void sample(uint16_t raw, uint16_t bpm, uint32_t ms) {
  if (ready.load()) return;
  samples[next] = raw;
  times[next] = ms;
  next = (next + 1) % CAPACITY;
  if (count < CAPACITY) ++count;
  // Freeze the preceding four seconds at the first measured-to-unavailable transition.
  if (hadRate && !bpm) {
    oldest = count == CAPACITY ? next : 0;
    ready.store(true);
  }
  hadRate = bpm != 0;
}
void poll(uint32_t ms) {
  if (!ready.load() || printed == count || ms - lastPrint < 500) return;
  lastPrint = ms;
  if (!printed) LOG("PTRACE_TIME,start=%lu,end=%lu,count=%u\n",
      (unsigned long)times[oldest],
      (unsigned long)times[(next + CAPACITY - 1) % CAPACITY], count);
  char text[129];
  const unsigned remaining = count - printed;
  const unsigned length = remaining < 32 ? remaining : 32;
  for (unsigned i = 0; i < length; ++i)
    snprintf(text + 4*i, 5, "%04x", samples[(oldest + printed + i) % CAPACITY]);
  LOG("PTRACE,%u,%s\n", printed, text);
  printed += length;
}
#else
void sample(uint16_t, uint16_t, uint32_t) {}
void poll(uint32_t) {}
#endif
}
