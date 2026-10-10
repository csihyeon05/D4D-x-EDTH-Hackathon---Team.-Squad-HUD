#pragma once
#include <Arduino.h>
#include "config.h"

// Single-master, open-drain software I2C. A whole transaction has a deadline:
// no transaction may stall sensor processing indefinitely.
class I2cBus {
public:
  enum class Error : uint8_t { None, Start, Address, Register, Restart, ReadAddress, Data, Stop, ClockTimeout, Deadline };
  static constexpr uint8_t SHORT_TIMEOUT_MS = 20, BURST_TIMEOUT_MS = 50;
  Error error() const { return error_; }
  uint32_t elapsedMs() const { return elapsedMs_; }
  uint8_t byteIndex() const { return byteIndex_; }
  void begin();
  // Call only between transactions; both pins are released by begin().
  void setPins(uint8_t sda, uint8_t scl) { sda_ = sda; scl_ = scl; }
  uint8_t sdaPin() const { return sda_; }
  uint8_t sclPin() const { return scl_; }
  bool probe(uint8_t address);
  bool read(uint8_t address, uint8_t reg, uint8_t* data, uint8_t count);
  bool write(uint8_t address, uint8_t reg, uint8_t value);
  bool writeBlock(uint8_t address, uint8_t reg, const uint8_t* data, uint8_t count);
private:
  uint8_t sda_ = PIN_WIRE_SDA, scl_ = PIN_WIRE_SCL;
  // Use RTOS milliseconds; vendor micros() assumes a fixed CPU/SysTick clock.
  uint32_t started_ = 0;
  uint8_t budgetMs_ = SHORT_TIMEOUT_MS, byteIndex_ = 0;
  uint32_t elapsedMs_ = 0;
  Error error_ = Error::None;
  bool step(bool ok, Error stage) { if (!ok && error_ == Error::None) error_ = stage; return ok; }
  bool clockHigh();
  bool start();
  bool stop();
  bool put(uint8_t value);
  bool get(uint8_t& value, bool ack);
};
