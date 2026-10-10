#pragma once
#include <Arduino.h>
#include "pulse_filter.h"

// Analog Pulse Sensor with PulseSensor Playground-style beat detection.
// Call sample() periodically at PULSE_PERIOD_US.
class PulseSensor {
public:
  explicit PulseSensor(uint8_t pin) : pin_(pin) {}

  void begin();
  void sample();

  uint16_t raw() const { return raw_; }  // 12bit
  uint16_t bpm() const { return bpm_; }  // 0 = unavailable
  uint16_t ibiMs() const { return ibi_; }
  int filtered() const { return filtered_; }
  uint32_t beatAgeMs(uint32_t now) const { return first_ ? UINT32_MAX : now - lastBeat_; }

private:
  // Threshold is in the filtered 10-bit domain, not the raw ADC domain.
  static constexpr int MIN_FILTERED_SWING = 4;
  void resetDetection(uint32_t now, int signal);
  void acceptInterval(uint16_t interval, uint32_t now);
  uint8_t intervalCount_ = 0;
  uint32_t lastConfirmedMs_ = 0;
  uint32_t lastClippedMs_ = 0;
  bool clipping_ = false;
  uint8_t  pin_;
  uint32_t previousMs_ = 0;
  bool sampled_ = false;

  PulseFilter filter_;
  int filtered_ = 512;
  uint16_t raw_ = 0;
  uint16_t bpm_ = 0;
  uint16_t ibi_ = 600;
  uint16_t confirmedIbi_ = 600;

  // Detection state (the signal is normalized to a 10-bit scale).
  uint32_t sampleCounter_ = 0;
  uint32_t lastBeat_ = 0;
  int thresh_ = 512, peak_ = 512, trough_ = 512, amp_ = 100;
  bool pulse_ = false, first_ = true;
  uint16_t rate_[5] = {0};
};
