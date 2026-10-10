#include <cassert>
#include "config.h"
#include "core/status_led.h"
#include "sensors/mpu6050_decode.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <deque>
#include "core/periodic.h"
#include "core/fractional_ticks.h"
#include "comm/i2c_bus.h"
#include "sensors/nmea_parser.h"
#include "sensors/pulse_sensor.h"
#include "sensors/tmp36.h"
uint32_t nowMs = 0, nowUs = 0;
bool busTest = false;
int adc = 0;
uint32_t millis() { return busTest ? nowUs/1000 : nowMs; }
int analogRead(uint8_t) { return adc; }
uint8_t pinModes[48]{};
unsigned pinChanges[48]{}, pinWrites[48]{};
uint32_t sclReleaseUs = 0;
bool slowRise = false;
int stuckPin = -1;
int clockReads = 0, holdClockFromRead = -1;
std::deque<int> sdaBits;
void pinMode(uint8_t pin, uint8_t mode) { pinModes[pin] = mode; ++pinChanges[pin];
  if (pin == PIN_WIRE_SCL && mode == INPUT_PULLUP) sclReleaseUs = nowUs; }
uint32_t micros() { return nowUs++; }
void delayMicroseconds(uint32_t us) { nowUs += us; }
void delay(uint32_t ms) { nowUs += ms * 1000; }
void digitalWrite(uint8_t pin, uint8_t) { ++pinWrites[pin]; }
int digitalRead(uint8_t pin) {
  if (pin == stuckPin || pinModes[pin] == OUTPUT) return 0;
  if (pin == PIN_WIRE_SCL && ++clockReads >= holdClockFromRead && holdClockFromRead >= 0)
    return 0;
  if (pin == PIN_WIRE_SCL && slowRise && uint32_t(nowUs - sclReleaseUs) < 8) return 0;
  if (pin == PIN_WIRE_SDA && !sdaBits.empty()) {
    const int bit = sdaBits.front(); sdaBits.pop_front(); return bit;
  }
  return 1;
}
std::string sentence(std::string body) {
  uint8_t sum = 0;
  for (char c : body) sum ^= c;
  char tail[8]; snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
  return "$" + body + tail;
}
void feed(NmeaParser& parser, const std::string& line, uint32_t now) {
  for (char c : line) parser.feed(c, now);
}
int main() {
  const uint8_t rawImu[] = {0x20,0, 0xE0,0, 0x80,0, 0x12,0x34, 0x02,0x8F, 0xFD,0x71, 0x7F,0xFF};
  const auto decoded = decodeMpu6050(rawImu, 123);
  assert(decoded.t_ms == 123 && decoded.ax == 8192 && decoded.ay == -8192);
  assert(decoded.az == -32768 && decoded.gx == 655 && decoded.gy == -655 && decoded.gz == 32767);

  FractionalTicks cadence(2000, 1024);
  uint32_t sumTicks = 0;
  for (uint32_t i = 1; i <= 500000; ++i) {
    uint32_t step = cadence.next();
    assert(step == 2 || step == 3);
    sumTicks += step;
    assert(sumTicks == uint64_t(i) * 2000 * 1024 / 1000000);
  }
  assert(sumTicks == 1024000); // 500Hz for 1000 seconds; no cumulative drift.
  Periodic tick(20);
  assert(!tick.due(19)); assert(tick.due(20)); assert(!tick.due(20));
  assert(tick.due(1000)); assert(!tick.due(1000)); assert(tick.due(1020));
  Periodic wrap(20); assert(wrap.due(UINT32_MAX - 10));
  assert(!wrap.due(5)); assert(wrap.due(9));
  const std::string gga = "GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
  NmeaParser gps;
  assert(!gps.snapshot(0).fix);
  feed(gps, sentence(gga), 100);
  auto p = gps.snapshot(100);
  assert(p.fix && p.sats == 8 && p.alt_cm == 54540);
  assert(std::abs(p.lat_e7 - 481173000) <= 1);
  assert(std::abs(p.lon_e7 - 115166667) <= 1);
  assert(!gps.snapshot(3100).fix && gps.snapshot(3100).lat_e7 == 0);
  feed(gps, sentence("GPRMC,123519,A,4807.038,N,01131.000,E,10.0,0,230394,,,A"), 200);
  assert(gps.snapshot(200).speed_cms == 514);
  feed(gps, sentence("GPRMC,123519,V,,,,,,,230394,,,N"), 300);
  assert(!gps.snapshot(300).fix);
  feed(gps, sentence(gga), 400);
  assert(!gps.snapshot(400).fix); // Fresh RMC says invalid.
  feed(gps, sentence("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,230394,,,A"), 500);
  assert(gps.snapshot(500).fix);
  auto bad = sentence(gga); bad[3] = 'X'; feed(gps, bad, 5000);
  assert(!gps.snapshot(5000).fix); // Bad checksum never refreshes fix.
  feed(gps, "$" + std::string(300, 'x') + "\n", 5100);
  feed(gps, sentence(gga), 5200); assert(gps.snapshot(5200).fix);
  feed(gps, sentence("GPGGA,123519,4867.038,N,01131.000,E,1,08,0.9,545.4,M,,,"), 5300);
  assert(!gps.snapshot(5300).fix); // Invalid minutes rejected.
  feed(gps, sentence("GPGGA,123519,4807.038,S,01131.000,W,1,08,0.9,-5.2,M,,,"), UINT32_MAX - 100);
  p = gps.snapshot(50); assert(p.fix && p.lat_e7 < 0 && p.lon_e7 < 0 && p.alt_cm == -520);
  feed(gps, sentence("GPGGA,123519,,,,,0,00,99.9,,,,,,"), 60);
  assert(!gps.snapshot(60).fix);
  NmeaParser speedExpiry;
  feed(speedExpiry, sentence(gga), 100);
  feed(speedExpiry, sentence("GPRMC,123519,A,4807.038,N,01131.000,E,10,0,230394,,,A"), 100);
  feed(speedExpiry, sentence(gga), 3000);
  assert(speedExpiry.snapshot(3100).fix && speedExpiry.snapshot(3100).speed_cms == 0);
  Tmp36 temp(2); temp.begin(); assert(temp.readCelsiusX100() == INT16_MIN);
  adc = 931; for (int i = 0; i < 16; ++i) temp.sample();
  assert(std::abs(temp.readCelsiusX100() - 2500) < 10);
  adc = 4095; for (int i = 0; i < 16; ++i) temp.sample();
  assert(temp.readCelsiusX100() == INT16_MIN);
  PulseSensor pulse(3); pulse.begin();
  for (nowMs = 0; nowMs < 10000; nowMs += 2) {
    adc = nowMs % 1000 >= 500 && nowMs % 1000 < 550 ? 2600 : 1800;
    pulse.sample();
  }
  assert(pulse.bpm() >= 59 && pulse.bpm() <= 61);
  nowMs += 100; pulse.sample(); assert(pulse.bpm() == 0);
  for (nowMs = 11000; nowMs < 15000; nowMs += 2) { adc = 1800; pulse.sample(); }
  assert(pulse.bpm() == 0);
  // DC offset must not prevent detection, and ADC noise must not become BPM.
  PulseSensor offsetPulse(3), noise(3);
  for (nowMs = 0; nowMs < 12000; nowMs += 2) {
    adc = nowMs % 1000 >= 500 && nowMs % 1000 < 550 ? 3500 : 3200;
    offsetPulse.sample();
    adc = 2048 + ((nowMs / 2) % 5) * 4;
    noise.sample();
    assert(noise.bpm() == 0);
  }
  assert(offsetPulse.bpm() >= 59 && offsetPulse.bpm() <= 61);
  // Clipping invalidates a previously valid BPM immediately and can recover.
  nowMs += 2; adc=3930; offsetPulse.sample(); assert(offsetPulse.bpm()==0);
  for (uint32_t elapsed=0; elapsed<12000; elapsed+=2) {
    nowMs+=2;
    adc = elapsed % 1000 >= 500 && elapsed % 1000 < 550 ? 3500 : 3200;
    offsetPulse.sample();
    if (elapsed<1000) assert(offsetPulse.bpm()==0);
  }
  assert(offsetPulse.bpm()>=59 && offsetPulse.bpm()<=61);
  nowMs+=2; adc=10; offsetPulse.sample(); assert(offsetPulse.bpm()==0);
  PulseSensor jittered(3);
  for (uint32_t elapsed = 0; elapsed < 10000; elapsed += elapsed % 3 + 1) {
    nowMs = (UINT32_MAX - 6000U) + elapsed;
    adc = elapsed % 1000 >= 500 && elapsed % 1000 < 550 ? 2600 : 1800;
    jittered.sample();
  }
  assert(jittered.bpm() >= 59 && jittered.bpm() <= 61);
  // Weak, clean optical pulses must not drop to unknown between beats.
  PulseSensor weak(3);
  for (nowMs = 0; nowMs < 30000; nowMs += 2) {
    const int amplitude = nowMs < 10000 ? 120 : 80;
    adc = 2048 + ((nowMs % 1000 >= 500 && nowMs % 1000 < 530) ? amplitude : 0);
    weak.sample();
    if (nowMs > 5000) assert(weak.bpm() >= 58 && weak.bpm() <= 62);
  }
  // A removed finger must still expire; no indefinite last-value hold.
  for (unsigned i = 0; i < 1600; ++i) { nowMs += 2; adc = 2048; weak.sample(); }
  assert(weak.bpm() == 0);
  // Mains-frequency interference alone must not look like fast heartbeats.
  for (int hz : {50, 60, 100, 120}) {
    PulseSensor interference(3), contaminated(3);
    for (nowMs = 0; nowMs < 20000; nowMs += 2) {
      const int noise = int(700 * sin(6.28318530718 * hz * nowMs / 1000.0));
      adc = 2048 + noise; interference.sample();
      if (nowMs > 3000) assert(interference.bpm() == 0);
      adc = 2048 + noise + ((nowMs % 1000 >= 500 && nowMs % 1000 < 550) ? 400 : 0);
      contaminated.sample();
    }
    assert(contaminated.bpm() >= 58 && contaminated.bpm() <= 62);
  }
  // Replay the recorded no-contact ADC noise without inventing a heart rate.
  FILE* trace = fopen("test/fixtures/pulse-no-contact.txt", "r");
  assert(trace);
  int recorded[1024];
  for (auto& value : recorded) assert(fscanf(trace, "%d", &value) == 1);
  fclose(trace);
  PulseSensor recordedNoise(3);
  for (unsigned i = 0; i < 1024 * 10; ++i) {
    nowMs = i * 2; adc = recorded[i % 1024]; recordedNoise.sample();
    assert(recordedNoise.bpm() == 0);
  }
  for (int expected : {40, 60, 90, 120, 180}) {
    PulseSensor cadence(3);
    for (nowMs = 0; nowMs < 30000; nowMs += 2) {
      adc = 2048 + int(300 * sin(6.28318530718 * expected * nowMs / 60000.0));
      cadence.sample();
    }
    assert(abs(int(cadence.bpm()) - expected) <= 2);
  }
  // Random UART bytes, malformed framing, then a valid sentence must recover.
  uint32_t random = 42;
  for (int i = 0; i < 50000; ++i) {
    random = random * 1664525U + 1013904223U;
    gps.feed((char)(random >> 24), 1000);
  }
  feed(gps, sentence(gga), 10000); assert(gps.snapshot(10000).fix);
  busTest = true;
  I2cBus bus;
  bus.begin(); uint8_t value = 0;
  assert(!bus.read(0x6A, 0x0F, &value, 1)); // No slave ACK.
  sdaBits = {1, 0, 1}; assert(bus.probe(0x68)); assert(sdaBits.empty());
  slowRise = true; const auto riseTestStart = nowUs;
  sdaBits = {1, 0, 0, 0, 1}; assert(bus.write(0x68,0x6C,0));
  assert(nowUs-riseTestStart < 2000); // Ordinary rise time must not incur 1ms per bit.
  slowRise = false;
  sdaBits = {1, 1, 1}; assert(!bus.probe(0x69)); assert(sdaBits.empty());
  assert(bus.error()==I2cBus::Error::Address);
  assert(!bus.probe(0) && !bus.probe(0x78));
  stuckPin = PIN_WIRE_SCL; nowUs = 0;
  assert(!bus.read(0x6A, 0x0F, &value, 1));
  assert(bus.error()==I2cBus::Error::ClockTimeout);
  assert(nowUs >= 20000 && nowUs < 21100); // Stuck clock cannot hang firmware.
  uint8_t burst[14]{}; nowUs = 0;
  assert(!bus.read(0x68, 0x3B, burst, sizeof(burst)));
  assert(nowUs >= 50000 && nowUs < 51100); // Long reads remain bounded.
  stuckPin = PIN_WIRE_SDA; nowUs = 0; bus.begin();
  assert(!bus.write(0x6A, 0x10, 0x48));
  assert(nowUs < 5000);
  stuckPin = -1;
  // Exercise successful register writes and repeated-start reads, not just NACKs.
  sdaBits = {1, 0, 0, 0, 1}; // Idle, address ACK, register ACK, data ACK, STOP.
  assert(bus.write(0x6A, 0x10, 0x38)); assert(sdaBits.empty());
  sdaBits = {1, 0, 0, 1, 0}; // Idle, addr ACK, reg ACK, repeated START, read addr ACK.
  for (uint8_t byte : {uint8_t(0x6A), uint8_t(0x80), uint8_t(0xFF)})
    for (int bit = 7; bit >= 0; --bit) sdaBits.push_back((byte >> bit) & 1);
  sdaBits.push_back(1); // STOP released SDA.
  uint8_t values[3]{};
  assert(bus.read(0x6A, 0x0F, values, 3));
  assert(values[0] == 0x6A && values[1] == 0x80 && values[2] == 0xFF);
  assert(sdaBits.empty());
  assert(!bus.read(0x6A, 0, nullptr, 1));
  assert(!bus.read(0x6A, 0, values, 0));
  assert(!bus.write(0xFF, 0, 0));
  // All bytes ACKed, but SCL stuck during STOP: this is still a failed transfer.
  sdaBits = {1, 0, 0, 0}; clockReads = 0; holdClockFromRead = 29;
  const uint32_t started = nowUs;
  assert(!bus.write(0x6A, 0x10, 0x38));
  assert(nowUs - started < 21500); // Deadline + up to one sleeping RTOS tick.
  holdClockFromRead = -1; sdaBits = {1, 0, 0, 0, 1};
  assert(bus.write(0x6A, 0x10, 0x38)); // Fault does not poison later transactions.
  sdaBits = {1, 0, 0, 0, 0};
  assert(!bus.write(0x6A, 0x10, 0x38)); // SDA must also release at STOP.
  // LED updates must never touch either shared I2C pin.
  const auto sdaModes = pinChanges[PIN_WIRE_SDA], sclModes = pinChanges[PIN_WIRE_SCL];
  const auto sdaWrites = pinWrites[PIN_WIRE_SDA], sclWrites = pinWrites[PIN_WIRE_SCL];
  StatusLed led; led.begin();
  for (auto state : {StatusLed::State::Boot, StatusLed::State::Reporting,
                    StatusLed::State::Baseline, StatusLed::State::SensorFault, StatusLed::State::Fatal})
    for (unsigned t=0; t<2100; t+=10) led.update(state,t);
  assert(pinChanges[PIN_WIRE_SDA]==sdaModes && pinChanges[PIN_WIRE_SCL]==sclModes);
  assert(pinWrites[PIN_WIRE_SDA]==sdaWrites && pinWrites[PIN_WIRE_SCL]==sclWrites);
  assert(pinWrites[STATUS_LED_BLUE]>0);
  puts("native firmware tests passed");
}

void logMessage(const char*, ...) {}
