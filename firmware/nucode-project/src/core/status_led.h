#pragma once
#include <stdint.h>

// Blue channel only: steady boot; one/two/three pulses for ready/connected/fault.
// Red/green channels remain unused during sensor diagnostics.
// Driven by loop(), so a stopped loop cannot keep emitting a healthy heartbeat.
class StatusLed {
public:
  enum class State : uint8_t { Boot, Ready, Connected, SensorFault, Fatal };
  void begin();
  void update(State state, uint32_t nowMs);
private:
  void set(bool on);
  State state_ = State::Boot;
  uint32_t sinceMs_ = 0;
  bool on_ = false;
};
