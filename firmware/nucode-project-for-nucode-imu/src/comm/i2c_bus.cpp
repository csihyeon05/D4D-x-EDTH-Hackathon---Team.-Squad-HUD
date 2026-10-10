#include "i2c_bus.h"
#include "config.h"
#include "i2c_hardware.h"
#include "core/log.h"
namespace {
void low(uint8_t pin) { digitalWrite(pin, LOW); pinMode(pin, OUTPUT); }
void release(uint8_t pin) { pinMode(pin, INPUT_PULLUP); }
void halfCycle() { delayMicroseconds(CFG_IMU_MODEL == 1 ? 10 : 25); }
}
bool I2cBus::clockHigh() {
  release(scl_);
  uint8_t risePolls = 0;
  while (!digitalRead(scl_)) {
    if ((uint32_t)(millis() - started_) >= budgetMs_) { error_ = Error::ClockTimeout; return false; }
    // Allow ordinary RC rise time without rounding every edge up to an RTOS tick.
    // A genuinely stretched/stuck clock still yields after a bounded short wait.
    if (risePolls++ < 25) delayMicroseconds(2);
    else delay(1);
  }
  halfCycle();
  if ((uint32_t)(millis() - started_) >= budgetMs_) { error_ = Error::Deadline; return false; }
  return true;
}
void I2cBus::begin() {
  HardwareI2c::end();
  hardware_ = false;
  release(sda_); release(scl_);
  error_ = Error::None; byteIndex_ = 0;
  started_ = millis(); budgetMs_ = SHORT_TIMEOUT_MS;
  // Recover a slave interrupted in the middle of a byte.
  for (uint8_t i = 0; i < 9 && !digitalRead(sda_); ++i) {
    low(scl_); halfCycle();
    if (!clockHigh()) break;
  }
  stop();
  if (CFG_IMU_MODEL == 2) hardware_ = HardwareI2c::begin(sda_, scl_);
  LOG("[i2c] SDA=%u SCL=%u hardware=%u\n", sda_, scl_, hardware_);
}
bool I2cBus::start() {
  release(sda_); halfCycle();
  if (!clockHigh() || !digitalRead(sda_)) return false;
  low(sda_); halfCycle(); low(scl_);
  return true;
}
bool I2cBus::stop() {
  low(scl_); low(sda_); halfCycle();
  const bool clockReleased = clockHigh();
  release(sda_); halfCycle();
  return clockReleased && digitalRead(sda_);
}
bool I2cBus::put(uint8_t value) {
  for (uint8_t mask = 0x80; mask; mask >>= 1) {
    if (value & mask) release(sda_); else low(sda_);
    halfCycle();
    if (!clockHigh()) return false;
    low(scl_);
  }
  release(sda_); halfCycle();
  if (!clockHigh()) return false;
  bool ack = !digitalRead(sda_);
  low(scl_);
  return ack;
}
bool I2cBus::get(uint8_t& value, bool ack) {
  value = 0; release(sda_);
  for (uint8_t i = 0; i < 8; ++i) {
    halfCycle();
    if (!clockHigh()) return false;
    value = (value << 1) | digitalRead(sda_);
    low(scl_);
  }
  if (ack) low(sda_); else release(sda_);
  halfCycle();
  if (!clockHigh()) return false;
  low(scl_); release(sda_);
  return true;
}
bool I2cBus::probe(uint8_t address) {
  if (address < 0x08 || address > 0x77) return false;
  if (hardware_) return hardwareTransfer(address, 0, nullptr, 0, false, true);
  error_ = Error::None; byteIndex_ = 0;
  started_ = millis(); budgetMs_ = SHORT_TIMEOUT_MS;
  const bool ack = step(start(),Error::Start) && step(put(address << 1),Error::Address);
  const Error firstError = error_;
  const bool stopped = step(stop(),Error::Stop);
  if (firstError != Error::None) error_ = firstError;
  elapsedMs_ = millis() - started_;
  return ack && stopped;
}
bool I2cBus::read(uint8_t address, uint8_t reg, uint8_t* data, uint8_t count) {
  if (address > 0x7F || !data || count == 0) return false;
  if (hardware_) return hardwareTransfer(address, reg, data, count, true, false);
  error_ = Error::None; byteIndex_ = 0;
  started_ = millis(); budgetMs_ = count > 4 ? BURST_TIMEOUT_MS : SHORT_TIMEOUT_MS;
  bool ok = step(start(),Error::Start) && step(put(address << 1),Error::Address)
    && step(put(reg),Error::Register) && step(start(),Error::Restart)
    && step(put((address << 1) | 1),Error::ReadAddress);
  for (uint8_t i = 0; ok && i < count; ++i) { byteIndex_ = i; ok = step(get(data[i], i + 1 < count),Error::Data); }
  const Error firstError = error_;
  const bool stopped = step(stop(),Error::Stop);
  if (firstError != Error::None) error_ = firstError;
  elapsedMs_ = millis() - started_;
  return ok && stopped;
}
bool I2cBus::write(uint8_t address, uint8_t reg, uint8_t value) {
  if (hardware_) return hardwareTransfer(address, reg, &value, 1, false, false);
  if (address > 0x7F) return false;
  error_ = Error::None; byteIndex_ = 0;
  started_ = millis(); budgetMs_ = SHORT_TIMEOUT_MS;
  bool ok = step(start(),Error::Start) && step(put(address << 1),Error::Address)
    && step(put(reg),Error::Register) && step(put(value),Error::Data);
  const Error firstError = error_;
  const bool stopped = step(stop(),Error::Stop);
  if (firstError != Error::None) error_ = firstError;
  elapsedMs_ = millis() - started_;
  return ok && stopped;
}

bool I2cBus::writeBlock(uint8_t address, uint8_t reg, const uint8_t* data, uint8_t count) {
  if (address > 0x7F || !data || !count || count > 16) return false;
  if (hardware_) {
    uint8_t buffer[16];
    for (uint8_t i=0;i<count;++i) buffer[i]=data[i];
    return hardwareTransfer(address, reg, buffer, count, false, false);
  }
  error_ = Error::None; byteIndex_ = 0;
  started_ = millis(); budgetMs_ = count > 4 ? BURST_TIMEOUT_MS : SHORT_TIMEOUT_MS;
  bool ok = step(start(),Error::Start) && step(put(address << 1),Error::Address) && step(put(reg),Error::Register);
  for (uint8_t i = 0; ok && i < count; ++i) ok = step(put(data[i]),Error::Data);
  const Error firstError = error_;
  const bool stopped = step(stop(),Error::Stop);
  if (firstError != Error::None) error_ = firstError;
  elapsedMs_ = millis() - started_;
  return ok && stopped;
}

bool I2cBus::hardwareTransfer(uint8_t address, uint8_t reg, uint8_t* data,
                              uint8_t count, bool read, bool probe) {
  const auto result = HardwareI2c::transfer(address, reg, data, count, read, probe,
      count > 4 ? BURST_TIMEOUT_MS : SHORT_TIMEOUT_MS);
  elapsedMs_ = result.elapsed;
  byteIndex_ = result.received;
  error_ = result.ok ? Error::None : result.timeout ? Error::Deadline :
      (result.abortSource & 1) ? Error::Address : Error::Data;
  return result.ok;
}
