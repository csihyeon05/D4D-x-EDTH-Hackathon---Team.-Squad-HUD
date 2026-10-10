#include "pulse_sensor.h"
#include "config.h"

void PulseSensor::begin() {
  pinMode(pin_, INPUT);
}

void PulseSensor::resetDetection(uint32_t now, int signal) {
  thresh_ = peak_ = trough_ = signal;
  amp_ = 0; first_ = true; second_ = false; pulse_ = false;
  bpm_ = 0; ibi_ = 600; lastBeat_ = now;
}

void PulseSensor::sample() {
  const uint32_t sample = analogRead(pin_);
  raw_ = sample > 4095 ? 4095 : sample;
  const int sig = raw_ >> (ADC_BITS - 10);  // Normalize to a 10-bit scale.

  const uint32_t now = millis();
  // A long sampling gap invalidates the beat history; never invent samples.
  if (!sampled_ || (uint32_t)(now - previousMs_) > 20) {
    resetDetection(now, sig);
  }
  sampled_ = true;
  previousMs_ = now;
  sampleCounter_ = now;
  // Clipped optical waveforms cannot establish valid inter-beat intervals.
  // Require one second clear of either rail before reacquiring beats.
  if (raw_ <= ADC_SCALE / 100 || raw_ >= ADC_SCALE * 95 / 100) {
    lastClippedMs_ = now;
    clipping_ = true;
  }
  if (clipping_ && uint32_t(now - lastClippedMs_) < 1000) {
    resetDetection(now, sig);
    return;
  }
  clipping_ = false;
  const int32_t n = (int32_t)(sampleCounter_ - lastBeat_);

  if (sig < thresh_ && n > (ibi_ / 5) * 3 && sig < trough_) trough_ = sig;
  if (sig > thresh_ && sig > peak_) peak_ = sig;

  if (n > 250) {
    if (sig > thresh_ && !pulse_ && n > (ibi_ / 5) * 3
        && peak_ - trough_ >= 8) {
      pulse_ = true;
      ibi_ = (uint16_t)(sampleCounter_ - lastBeat_);
      lastBeat_ = sampleCounter_;

      if (second_) {
        second_ = false;
        for (auto& r : rate_) r = ibi_;
      }
      if (first_) {
        first_ = false;
        second_ = true;
        return;
      }

      uint32_t total = 0;
      for (int i = 0; i < 9; i++) {
        rate_[i] = rate_[i + 1];
        total += rate_[i];
      }
      rate_[9] = ibi_;
      total += ibi_;
      bpm_ = (uint16_t)(60000UL * 10 / total);
    }
  }

  if (sig <= thresh_ && pulse_) {
    pulse_ = false;
    amp_ = peak_ - trough_;
    thresh_ = amp_ / 2 + trough_;
    peak_ = thresh_;
    trough_ = thresh_;
  }

  // Reset after 2.5 seconds without a detected beat.
  if (n > 2500) resetDetection(now, sig);
}
