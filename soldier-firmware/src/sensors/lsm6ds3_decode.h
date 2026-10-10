#pragma once
#include "protocol/packets.h"

inline int16_t lsmSigned16(const uint8_t* p) {
  const uint16_t value = uint16_t(p[0]) | (uint16_t(p[1]) << 8);
  return value <= 32767 ? int16_t(value) : int16_t(int32_t(value) - 65536);
}
// OUTX_L_G through OUTZ_H_XL: gyro first, then acceleration, little-endian.
inline ImuPacket decodeLsm6ds3(const uint8_t* p, uint32_t time) {
  return {time, lsmSigned16(p+6), lsmSigned16(p+8), lsmSigned16(p+10),
          lsmSigned16(p), lsmSigned16(p+2), lsmSigned16(p+4)};
}
