#pragma once
#include <stdarg.h>
#include <stdio.h>
#include "packets.h"

namespace nus {
// Integer wire units avoid target printf floating-point dependencies.
class Json {
public:
  Json(char* out, size_t cap, uint8_t id, uint32_t seq, const char* type, uint32_t ms)
      : out_(out), cap_(cap) {
    append("{\"v\":1,\"id\":%u,\"n\":%lu,\"type\":\"%s\",\"ms\":%lu",
           id, (unsigned long)seq, type, (unsigned long)ms);
  }
  void append(const char* fmt, ...) {
    if (!valid_) return;
    va_list args; va_start(args, fmt);
    const int n = vsnprintf(out_ + length_, cap_ - length_, fmt, args);
    va_end(args);
    if (n < 0 || size_t(n) >= cap_ - length_) { valid_ = false; return; }
    length_ += size_t(n);
  }
  size_t finish() { append("}\n"); return valid_ ? length_ : 0; }
private:
  char* out_; size_t cap_, length_ = 0; bool valid_ = true;
};
inline size_t encode(char* b, size_t size, uint8_t id, uint32_t n, const char* model, const ImuPacket& p) {
  Json j(b,size,id,n,"imu",p.t_ms);
  j.append(",\"model\":\"%s\",\"a\":[%d,%d,%d],\"g\":[%d,%d,%d]",model,p.ax,p.ay,p.az,p.gx,p.gy,p.gz);
  return j.finish();
}
inline size_t encode(char* b, size_t size, uint8_t id, uint32_t n, const VitalsPacket& p) {
  Json j(b,size,id,n,"vitals",p.t_ms);
  if (p.temp_c_x100 == INT16_MIN) j.append(",\"tc\":null"); else j.append(",\"tc\":%d",p.temp_c_x100);
  j.append(",\"bpm\":%u,\"pulse\":%u",p.bpm,p.pulse_raw); return j.finish();
}
inline size_t encode(char* b, size_t size, uint8_t id, uint32_t n, const GpsPacket& p) {
  Json j(b,size,id,n,"gps",p.t_ms);
  j.append(",\"fix\":%u,\"lat_e7\":%ld,\"lon_e7\":%ld,\"alt_cm\":%ld,\"speed_cms\":%u,\"sats\":%u",p.fix,(long)p.lat_e7,(long)p.lon_e7,(long)p.alt_cm,p.speed_cms,p.sats);
  return j.finish();
}
inline size_t encode(char* b, size_t size, uint8_t id, uint32_t n, const StatusPacket& p) {
  Json j(b,size,id,n,"status",p.t_ms);
  j.append(",\"sensor_v\":%u,\"flags\":%u,\"dropped\":%lu,\"gaps\":%lu",p.version,p.flags,(unsigned long)p.tx_dropped,(unsigned long)p.sample_gaps); return j.finish();
}
inline size_t encode(char* b, size_t size, uint8_t id, uint32_t n, const OrientationPacket& p) {
  Json j(b,size,id,n,"quat",p.t_ms);
  j.append(",\"q30\":[%ld,%ld,%ld,%ld]",(long)p.w,(long)p.x,(long)p.y,(long)p.z); return j.finish();
}
}
