#pragma once
#include <Arduino.h>
#include "protocol/packets.h"

// NU-IMU: LSM6DS3 (0x69) or LSM6DS3TR-C (0x6A) identity.
class Imu {
public:
  enum class State : uint8_t { Offline, ResetWait, Configure, Settling, RawRunning };
  bool begin();
  void poll();
  bool read(ImuPacket& out, OrientationPacket& orientation);
  bool ok() const { return state_ == State::RawRunning; }
  bool hasOrientation() const { return false; }
  bool active() const { return state_ != State::Offline; }
  State state() const { return state_; }
  uint32_t samples() const { return samples_; }
  uint32_t readErrors() const { return readErrors_; }
  uint32_t restarts() const { return restarts_; }
  uint32_t fifoResets() const { return 0; }
private:
  void fail();
  void readFailure();
  bool checked(uint8_t reg, uint8_t value);
  State state_ = State::Offline;
  uint32_t sinceMs_ = 0, lastDataMs_ = 0, lastPollMs_ = 0;
  uint32_t samples_ = 0, readErrors_ = 0, restarts_ = 0;
  uint8_t addr_ = 0, failures_ = 0, step_ = 0;
  bool fresh_ = false;
  ImuPacket sample_{};
};
