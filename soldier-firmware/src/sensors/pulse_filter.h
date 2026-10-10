#pragma once
#include <stdint.h>

// Two low-pass stages (~4 Hz each) suppress fast electrical interference.
// A slow baseline removes DC drift; timestamps preserve behavior across jitter.
class PulseFilter {
public:
  void reset(float signal) { first_ = second_ = baseline_ = signal; }
  int update(float signal, uint32_t dtMs) {
    const float fast = dtMs / (40.0f + dtMs);
    const float slow = dtMs / (1000.0f + dtMs);
    first_ += fast * (signal - first_);
    second_ += fast * (first_ - second_);
    baseline_ += slow * (second_ - baseline_);
    return int(512.5f + second_ - baseline_);
  }
private:
  float first_ = 0, second_ = 0, baseline_ = 0;
};
