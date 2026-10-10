#pragma once
#include <math.h>
#include <stdint.h>

namespace node {
// Relative heading only, for a board mounted with z vertical. No absolute yaw.
// Calibrate gyro bias from 100 consecutive stationary samples before using PDR.
class RawHeading {
public:
  bool update(uint32_t ms, float accelG, float gx, float gy, float gz) {
    const uint32_t dt = ms - lastMs_;
    lastMs_ = ms;
    if (!ready_) {
      if (fabsf(accelG - 1.0f) > 0.15f || fabsf(gx)>5 || fabsf(gy)>5 || fabsf(gz)>5 || dt>100) {
        count_=0; sum_=0; return false;
      }
      sum_ += gz;
      if (++count_ >= 100) { bias_=sum_/count_; ready_=true; }
      return ready_;
    }
    // Never integrate a long sensor outage as continuous rotation.
    if (dt <= 100) heading_ -= (gz-bias_) * 0.01745329252f * dt / 1000.0f;
    heading_ = remainderf(heading_,6.28318530718f);
    return true;
  }
  bool ready() const { return ready_; }
  float headingRad() const { return heading_; }
private:
  uint32_t lastMs_=0;
  unsigned count_=0;
  float sum_=0,bias_=0,heading_=0;
  bool ready_=false;
};
}
