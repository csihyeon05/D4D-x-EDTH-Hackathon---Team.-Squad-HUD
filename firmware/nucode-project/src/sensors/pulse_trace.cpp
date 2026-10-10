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
uint16_t samples[1024];
uint16_t count=0, printed=0;
uint32_t startMs=0,endMs=0,lastPrint=0;
std::atomic<bool> ready{false};
}
void sample(uint16_t raw,uint32_t ms) {
  if (ms<30000 || count==1024) return;
  if (!count) startMs=ms;
  samples[count++]=raw;
  if (count==1024) {endMs=ms;ready.store(true);}
}
void poll(uint32_t ms) {
  if (!ready.load() || printed==1024 || ms-lastPrint<500) return;
  lastPrint=ms;
  if (!printed) LOG("PTRACE_TIME,start=%lu,end=%lu\n",(unsigned long)startMs,(unsigned long)endMs);
  char text[129];
  for (unsigned i=0;i<32;++i) snprintf(text+4*i,5,"%04x",samples[printed+i]);
  LOG("PTRACE,%u,%s\n",printed,text);
  printed+=32;
}
#else
void sample(uint16_t,uint32_t) {}
void poll(uint32_t) {}
#endif
}
