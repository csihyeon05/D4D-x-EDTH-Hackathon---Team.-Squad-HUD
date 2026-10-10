#pragma once
#include <Arduino.h>
namespace HardwareI2c {
struct Result { bool ok; uint32_t elapsed; uint32_t abortSource; bool timeout; uint8_t received; };
#ifdef BOARD_AMB26
void end();
bool begin(uint8_t sda, uint8_t scl);
Result transfer(uint8_t address, uint8_t reg, uint8_t* data, uint8_t count, bool read, bool probe, uint8_t budget);
#else
inline void end() {}
inline bool begin(uint8_t, uint8_t) { return false; }
inline Result transfer(uint8_t, uint8_t, uint8_t*, uint8_t, bool, bool, uint8_t) { return {false,0,0,false,0}; }
#endif
}
