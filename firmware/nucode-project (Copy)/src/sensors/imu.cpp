#include "imu.h"
#include "lsm6ds3_decode.h"
#include "comm/i2c_bus.h"
#include "core/log.h"
#include <initializer_list>

namespace { I2cBus bus; }
void Imu::fail() {
  LOG("[imu] LSM6DS3 offline state=%u; retrying\n", unsigned(state_));
  state_ = State::Offline;
  fresh_ = false;
}
void Imu::readFailure() {
  ++readErrors_;
  fresh_ = false;
  LOG("[imu] read failed err=%u byte=%u ms=%lu\n", unsigned(bus.error()),
      bus.byteIndex(), (unsigned long)bus.elapsedMs());
  bus.begin();
  if (++failures_ >= 3) fail();
}
bool Imu::checked(uint8_t reg, uint8_t value) {
  uint8_t actual = 0;
  const bool valid = bus.write(addr_, reg, value) && bus.read(addr_, reg, &actual, 1);
  if (!valid || actual != value)
    LOG("[imu] reg=0x%02X expected=0x%02X actual=0x%02X err=%u\n",
        reg, value, actual, unsigned(bus.error()));
  return valid && actual == value;
}
bool Imu::begin() {
  ++restarts_;
  state_ = State::Offline; fresh_ = false; failures_ = 0; step_ = 0;
  for (uint8_t reversed = 0; reversed < 2; ++reversed) {
    bus.setPins(reversed ? PIN_WIRE_SCL : PIN_WIRE_SDA,
                reversed ? PIN_WIRE_SDA : PIN_WIRE_SCL);
    bus.begin();
    for (uint8_t address : {0x6A, 0x6B}) {
      uint8_t id = 0;
      const bool ack = bus.probe(address);
      const bool responds = ack && bus.read(address, 0x0F, &id, 1);
      LOG("[imu] SDA=%s SCL=%s addr=0x%02X ack=%u id=0x%02X\n",
          reversed ? IMU_SCL_LABEL : IMU_SDA_LABEL,
          reversed ? IMU_SDA_LABEL : IMU_SCL_LABEL, address, ack, id);
      if (!responds || (id != 0x69 && id != 0x6A)) continue;
      addr_ = address;
      if (!bus.write(addr_, 0x12, 0x01)) return false;
      sinceMs_ = millis(); state_ = State::ResetWait;
      LOG("[imu] NU-IMU WHO_AM_I=0x%02X; resetting\n", id);
      return true;
    }
  }
  LOG("[imu] NU-IMU not found; check 3.3V/GND/SDA/SCL\n");
  return false;
}
void Imu::poll() {
  const uint32_t now = millis();
  if (!active()) return;
  if (state_ == State::ResetWait) {
    if (uint32_t(now - sinceMs_) > 500) { fail(); return; }
    if (uint32_t(now - sinceMs_) < 10) return;
    uint8_t reset = 0;
    if (!bus.read(addr_, 0x12, &reset, 1)) { readFailure(); return; }
    if (!(reset & 1)) { failures_ = 0; state_ = State::Configure; }
    return;
  }
  if (state_ == State::Configure) {
    // BDU and auto-increment prevent torn words. FIFO and interrupts are unused.
    // 52 Hz, +/-4 g, +/-500 dps; normal/low-power operation at this ODR.
    static const uint8_t settings[][2] = {
      {0x12,0x44}, {0x0A,0x00}, {0x0D,0x00}, {0x0E,0x00},
      {0x15,0x10}, {0x16,0x80}, {0x10,0x38}, {0x11,0x34}
    };
    if (!checked(settings[step_][0], settings[step_][1])) { readFailure(); return; }
    failures_ = 0;
    if (++step_ == sizeof(settings)/sizeof(settings[0])) {
      state_ = State::Settling; sinceMs_ = millis();
    }
    return;
  }
  if (state_ == State::Settling) {
    if (uint32_t(now - sinceMs_) < 150) return;
    state_ = State::RawRunning; lastDataMs_ = now; lastPollMs_ = now;
    LOG("[imu] ready 52Hz +/-4g +/-500dps; g=raw*0.000122 dps=raw*0.0175\n");
  }
  if (uint32_t(now - lastDataMs_) > 1000) { fail(); return; }
  if (uint32_t(now - lastPollMs_) < 5) return;
  lastPollMs_ = now;
  uint8_t status = 0, bytes[12];
  if (!bus.read(addr_, 0x1E, &status, 1)) { readFailure(); return; }
  if ((status & 3) != 3) return;
  if (!bus.read(addr_, 0x22, bytes, sizeof(bytes))) { readFailure(); return; }
  sample_ = decodeLsm6ds3(bytes, millis());
  ++samples_; fresh_ = true; failures_ = 0; lastDataMs_ = millis();
}
bool Imu::read(ImuPacket& out, OrientationPacket& orientation) {
  if (!ok() || !fresh_) return false;
  fresh_ = false; out = sample_; orientation = {};
  return true;
}
