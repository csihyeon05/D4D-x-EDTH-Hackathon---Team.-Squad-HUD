#include "imu.h"
#include "comm/i2c_bus.h"
#include "core/log.h"
#include "vendor/motionapps612.h"
#include <initializer_list>
#include <string.h>

namespace { I2cBus bus; }
void Imu::fail() {
  LOG("[imu] IMU offline state=%u loaded=%u; retrying\n", unsigned(state_), loaded_);
  state_ = State::Offline; fresh_ = false;
}
bool Imu::checked(uint8_t reg, uint8_t value) {
  uint8_t actual = 0;
  const bool written = bus.write(addr_, reg, value);
  const bool read = written && bus.read(addr_, reg, &actual, 1);
  if (!read || actual != value)
    LOG("[imu] reg=0x%02X expected=0x%02X actual=0x%02X write=%u read=%u err=%u ms=%lu\n", reg, value, actual, written, read, unsigned(bus.error()), (unsigned long)bus.elapsedMs());
  return read && actual == value;
}
bool Imu::begin() {
  ++restarts_;
  state_ = State::Offline; fresh_ = false;
  loaded_ = 0; failures_ = 0; rawStep_ = 0; stillness_.reset();
  for (uint8_t reversed = 0; reversed < 2; ++reversed) {
    bus.setPins(reversed ? PIN_WIRE_SCL : PIN_WIRE_SDA,
                reversed ? PIN_WIRE_SDA : PIN_WIRE_SCL);
    const char* sdaLabel = reversed ? IMU_SCL_LABEL : IMU_SDA_LABEL;
    const char* sclLabel = reversed ? IMU_SDA_LABEL : IMU_SCL_LABEL;
    bus.begin();
    for (uint8_t addr : {0x68, 0x69}) {
      uint8_t id = 0;
      const bool ack = bus.probe(addr);
      const bool responds = ack && bus.read(addr, 0x75, &id, 1);
      if (!responds || id != 0x68) {
        LOG("[imu] SDA=%s SCL=%s addr=0x%02X ack=%u read=%u id=0x%02X\n",
            sdaLabel, sclLabel, addr, ack, responds, id);
        continue;
      }
      addr_ = addr;
      if (!bus.write(addr_, 0x6B, 0x80)) return false;
      sinceMs_ = millis(); state_ = State::ResetWait;
      LOG("[imu] MPU6050 0x%02X SDA=%s SCL=%s; mode=%s\n",
          addr_, sdaLabel, sclLabel, CFG_IMU_DMP ? "DMP" : "raw");
      return true;
    }
    LOG("[imu] idle SDA=%u SCL=%u (both should be 1)\n",
        digitalRead(bus.sdaPin()), digitalRead(bus.sclPin()));
  }
  LOG("[imu] MPU6050 not found on %s/%s in either direction\n", IMU_SDA_LABEL, IMU_SCL_LABEL);
  return false;
}
bool Imu::uploadChunk() {
  const uint8_t bank = loaded_ >> 8, offset = loaded_ & 255;
  const uint16_t left = sizeof(DMP_FIRMWARE) - loaded_;
  const uint8_t count = left < 16 ? left : 16;
  uint8_t verify[16];
  if (!bus.write(addr_, 0x6D, bank) || !bus.write(addr_, 0x6E, offset)
      || !bus.writeBlock(addr_, 0x6F, DMP_FIRMWARE + loaded_, count)
      || !bus.write(addr_, 0x6D, bank) || !bus.write(addr_, 0x6E, offset)
      || !bus.read(addr_, 0x6F, verify, count)
      || memcmp(verify, DMP_FIRMWARE + loaded_, count)) return false;
  loaded_ += count;
  return true;
}
bool Imu::resetFifo() {
  fresh_ = false; stillness_.reset(); ++fifoResets_;
  // Stop DMP before resetting FIFO, then restart without reloading its image.
  return bus.write(addr_, 0x6A, 0x40) && bus.write(addr_, 0x6A, 0x44)
      && bus.write(addr_, 0x6A, 0xC0);
}
void Imu::poll() {
  const uint32_t now = millis();
  if (!active()) return;
  if (state_ == State::ResetWait) {
    if (uint32_t(now - sinceMs_) < 100) return;
    if (!CFG_IMU_DMP) { state_ = State::RawConfig; return; }
    if (!bus.write(addr_, 0x6A, 0x07)) { fail(); return; }
    sinceMs_ = now; state_ = State::SignalWait; return;
  }
  if (state_ == State::RawConfig) {
    // One checked setting per loop gives the sensor time after clock/power changes.
    static const uint8_t settings[][2] = {
      {0x6B,0x01}, {0x6C,0x00}, {0x6A,0x00}, {0x23,0x00},
      {0x1A,0x03}, {0x19,0x09}, {0x1B,0x18}, {0x1C,0x00}, {0x38,0x01}
    };
    if (!checked(settings[rawStep_][0], settings[rawStep_][1])) {
      bus.begin();
      if (++failures_ >= 3) fail();
      return;
    }
    failures_ = 0;
    if (++rawStep_ == sizeof(settings)/sizeof(settings[0])) {
      state_ = State::RawRunning; lastDataMs_ = now;
      LOG("[imu] raw mode ready; accel +/-2g gyro +/-2000dps\n");
    }
    return;
  }
  if (state_ == State::RawRunning) {
    if (uint32_t(now-lastDataMs_) > 1000) { fail(); return; }
    if (uint32_t(now-lastPollMs_) < 10) return;
    lastPollMs_ = now;
    uint8_t status = 0, bytes[14];
    if (!bus.read(addr_,0x3A,&status,1)) {
      ++readErrors_;
      LOG("[imu] status fail err=%u byte=%u ms=%lu\n", unsigned(bus.error()), bus.byteIndex(), (unsigned long)bus.elapsedMs());
      fresh_ = false;
      if (++failures_ >= 3) fail();
      return;
    }
    if (!(status & 1)) return;
    const uint32_t readStarted = millis();
    if (!bus.read(addr_,0x3B,bytes,sizeof(bytes))) {
      ++readErrors_;
      LOG("[imu] burst fail err=%u byte=%u ms=%lu\n", unsigned(bus.error()), bus.byteIndex(), (unsigned long)(millis()-readStarted));
      fresh_ = false;
      if (++failures_ >= 3) fail();
      return;
    }
    ++samples_;
    sample_ = decodeMpu6050(bytes,millis());
    orientation_ = {}; // No synthetic quaternion in raw mode.
    fresh_ = true; failures_ = 0; lastDataMs_ = millis();
    return;
  }
  if (state_ == State::SignalWait) {
    if (uint32_t(now - sinceMs_) < 100) return;
    // MotionApps 6.12 requires +/-2g and +/-2000dps, unlike raw-mode v2.
    if (!(checked(0x6B, 0x01) && checked(0x6C, 0x00) && checked(0x38, 0x00)
       && checked(0x23, 0x00) && checked(0x1C, 0x00) && checked(0x37, 0x80)
       && checked(0x19, 0x04) && checked(0x1A, 0x01) && checked(0x1B, 0x18)
       && checked(0x6A, 0x00))) { fail(); return; }
    state_ = State::Upload; return;
  }
  if (state_ == State::Upload) {
    // One bank-aligned block per loop; no multi-second blocking upload.
    if (!uploadChunk()) { fail(); return; }
    if (loaded_ == sizeof(DMP_FIRMWARE)) state_ = State::Start;
    return;
  }
  if (state_ == State::Start) {
    if (!(checked(0x70, 0x04) && checked(0x71, 0x00) && checked(0x38, 0x02)
       && resetFifo())) { fail(); return; }
    state_ = State::Settling; sinceMs_ = now; lastDataMs_ = now;
    LOG("[imu] DMP loaded; keep still for at least 10 seconds\n");
    return;
  }
  if (uint32_t(now - lastDataMs_) > 1000) { fail(); return; }
  if (uint32_t(now - lastPollMs_) < 5) return;
  lastPollMs_ = now;
  uint8_t status = 0, countBytes[2];
  if (!bus.read(addr_, 0x3A, &status, 1) || !bus.read(addr_, 0x72, countBytes, 2)) {
    fresh_ = false;
    if (++failures_ >= 3) fail();
    return;
  }
  const uint16_t count = uint16_t(countBytes[0]) << 8 | countBytes[1];
  if ((status & 0x10) || count > DMP_PACKET_BYTES * 4) {
    if (!resetFifo()) fail();
    return;
  }
  // A partial tail can be a packet in flight; do not mistake it for corruption.
  const uint8_t packets = count / DMP_PACKET_BYTES;
  for (uint8_t i = 0; i < packets && i < 2; ++i) {
    uint8_t bytes[DMP_PACKET_BYTES];
    // Each FIFO burst retains the bounded burst transaction deadline.
    if (!bus.read(addr_, 0x74, bytes, 14) || !bus.read(addr_, 0x74, bytes + 14, 14)) {
      if (!resetFifo() || ++failures_ >= 3) fail();
      return;
    }
    ImuPacket sample;
    OrientationPacket quaternion;
    if (!decodeDmp(bytes, millis(), sample, quaternion)) {
      if (!resetFifo() || ++failures_ >= 3) fail();
      return;
    }
    failures_ = 0; lastDataMs_ = millis();
    if (state_ == State::Settling) {
      if (!stillness_.add(sample) || uint32_t(now - sinceMs_) < 10000) continue;
      state_ = State::Running;
      LOG("[imu] DMP ready; yaw reference is startup-relative\n");
    }
    sample_ = sample; orientation_ = quaternion; fresh_ = true;
  }
}
bool Imu::read(ImuPacket& out, OrientationPacket& orientation) {
  if (!ok() || !fresh_) return false;
  fresh_ = false; out = sample_; orientation = orientation_;
  return true;
}
