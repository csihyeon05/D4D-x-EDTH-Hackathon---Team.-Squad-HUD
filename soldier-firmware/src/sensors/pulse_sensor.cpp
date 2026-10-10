#include "pulse_sensor.h"
#include "config.h"

void PulseSensor::begin() {
  pinMode(pin_, INPUT);
}

void PulseSensor::resetDetection(uint32_t now, int signal) {
  filtered_ = signal;
  thresh_ = peak_ = trough_ = signal;
  amp_ = 0; first_ = true; pulse_ = false;
  bpm_ = 0; ibi_ = confirmedIbi_ = 600; lastBeat_ = now; intervalCount_ = 0;
}

void PulseSensor::acceptInterval(uint16_t interval, uint32_t now) {
  // Reacquire after inconsistent timing instead of averaging artifacts into BPM.
  if (intervalCount_) {
    uint16_t ordered[5];
    for (uint8_t i = 0; i < intervalCount_; ++i) ordered[i] = rate_[i];
    for (uint8_t i = 1; i < intervalCount_; ++i) {
      for (uint8_t j = i; j && ordered[j] < ordered[j - 1]; --j) {
        const auto value = ordered[j]; ordered[j] = ordered[j - 1]; ordered[j - 1] = value;
      }
    }
    const uint16_t reference = ordered[intervalCount_ / 2];
    if (uint32_t(interval) * 4 < uint32_t(reference) * 3 ||
        uint32_t(interval) * 4 > uint32_t(reference) * 5) intervalCount_ = 0;
  }
  if (intervalCount_ == 5) {
    for (uint8_t i = 0; i < 4; ++i) rate_[i] = rate_[i + 1];
    intervalCount_ = 4;
  }
  rate_[intervalCount_++] = interval;
  if (intervalCount_ < 3) return;
  uint16_t ordered[5];
  for (uint8_t i = 0; i < intervalCount_; ++i) ordered[i] = rate_[i];
  for (uint8_t i = 1; i < intervalCount_; ++i) {
    for (uint8_t j = i; j && ordered[j] < ordered[j - 1]; --j) {
      const auto value = ordered[j]; ordered[j] = ordered[j - 1]; ordered[j - 1] = value;
    }
  }
  confirmedIbi_ = ordered[intervalCount_ / 2];
  bpm_ = 60000UL / confirmedIbi_;
  lastConfirmedMs_ = now;
}

void PulseSensor::sample() {
  const uint32_t sample = analogRead(pin_);
  raw_ = sample > 4095 ? 4095 : sample;
  const int input = raw_ >> (ADC_BITS - 10);  // Normalize to a 10-bit scale.

  const uint32_t now = millis();
  // A long sampling gap invalidates the beat history; never invent samples.
  const uint32_t dt = sampled_ ? uint32_t(now - previousMs_) : 0;
  if (!sampled_ || dt > 20) {
    filter_.reset(input);
    resetDetection(now, 512);
  }
  sampled_ = true;
  previousMs_ = now;
  sampleCounter_ = now;
  if (bpm_ && uint32_t(now - lastConfirmedMs_) > 2500) bpm_ = 0;
  // Clipped optical waveforms cannot establish valid inter-beat intervals.
  // Require one second clear of either rail before reacquiring beats.
  if (raw_ <= ADC_SCALE / 100 || raw_ >= ADC_SCALE * 95 / 100) {
    lastClippedMs_ = now;
    clipping_ = true;
  }
  if (clipping_ && uint32_t(now - lastClippedMs_) < 1000) {
    filter_.reset(input);
    resetDetection(now, 512);
    return;
  }
  clipping_ = false;
  const int sig = filter_.update(input, dt <= 20 ? dt : 0);
  filtered_ = sig;
  const int32_t n = (int32_t)(sampleCounter_ - lastBeat_);

  // One missed beat must not double the refractory period and hide every other beat.
  const uint16_t cadence = ibi_ < confirmedIbi_ ? ibi_ : confirmedIbi_;
  // Forget an old large pulse before it can mask a smaller subsequent pulse.
  // Follow the local baseline while preserving the minimum swing requirement.
  if (n > (cadence / 5) * 3) {
    const float decay = dt / (150.0f + dt);
    thresh_ += decay * (sig - thresh_);
    peak_ += decay * (sig - peak_);
    trough_ += decay * (sig - trough_);
  }
  if (sig < thresh_ && n > (cadence / 5) * 3 && sig < trough_) trough_ = sig;
  if (sig > thresh_ && sig > peak_) peak_ = sig;

  if (n > 250) {
    if (sig > thresh_ && !pulse_ && n > (cadence / 5) * 3
        && peak_ - trough_ >= MIN_FILTERED_SWING) {
      pulse_ = true;
      ibi_ = (uint16_t)(sampleCounter_ - lastBeat_);
      lastBeat_ = sampleCounter_;

      if (first_) {
        first_ = false;
        return;
      }
      acceptInterval(ibi_, now);
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
