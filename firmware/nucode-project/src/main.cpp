#include <Arduino.h>
#include "config.h"
#include "core/log.h"
#include "core/periodic.h"
#include "core/status_led.h"
#include "comm/advertiser.h"
#include "comm/serial_telemetry.h"
#include "node/motion.h"
#include "node/raw_heading.h"
#include "node/packet.h"
#include "node/position.h"
#include "node/rules.h"
#include "node/schedule.h"
#include "sensors/analog_sensors.h"
#include "sensors/gps.h"
#include "sensors/imu.h"
#include "sensors/pulse_trace.h"

namespace {
Imu imu;
AnalogSensors analog;
Gps gps;
Advertiser radio;
StatusLed statusLed;
uint32_t readyMs = 0;
bool analogReady = false, radioReady = false;
uint32_t sampleGaps = 0;
VitalsPacket vitals{0, INT16_MIN, 0, 0};
Periodic imuTick(IMU_PERIOD_US), vitalsTick(VITALS_PERIOD_US);
Periodic nodeTick(NODE_PERIOD_US), retryTick(IMU_RETRY_US);

node::ActivityMeter activity(ACCEL_G_PER_LSB);
node::RawHeading rawHeading;
bool headingValid = false;
node::Pdr pdr;
node::PositionFusion position;
node::NodeRules rules;
node::Baseline baseline;
node::CheckinTimer checkins;
uint16_t seq = 0;

// Once per second: evaluate rules and send an alert or a due check-in.
void nodeStep(uint32_t nowMs) {
  const uint32_t tS = nowMs / 1000;
  const node::ActivityMeter::Second motion = activity.roll();
  GpsPacket fix{};
  gps.snapshot(fix);
  position.update(node::GeoFix{fix.fix != 0, fix.lat_e7, fix.lon_e7}, pdr.x(), pdr.y());

  const bool fresh = analogReady && nowMs - vitals.t_ms < 1000;
  const bool hrValid = fresh && vitals.bpm != 0;
  const bool tempValid = fresh && vitals.temp_c_x100 != INT16_MIN;
  const float hr = vitals.bpm, temp = vitals.temp_c_x100 / 100.0f;

  baseline.add(tS, hrValid, hr, tempValid, temp);
  bool firstCheckin = false;
  if (baseline.done() && !checkins.started()) {
    rules.setBaseline(baseline.hrValid(), baseline.hrBpm(), baseline.tempValid(), baseline.skinTempC());
    checkins.start(tS);
    firstCheckin = true;
  }

  // An axis at the configured sensor limit hides the true peak; treat it as an impact.
  const float peakG = motion.saturated && motion.peakG < node::FALL_IMPACT_G ? node::FALL_IMPACT_G : motion.peakG;
  const node::RuleSample sample{tS, hrValid, hr, tempValid, temp, peakG,
                                motion.valid, motion.activity, 0, node::AlertCode::None};
  const node::AlertCode code = rules.evaluate(sample);

  node::NodePacket packet{};
  if (code != node::AlertCode::None) {
    packet.type = node::PacketType::Alert;
  } else if (checkins.due(tS)) {
    packet.type = node::PacketType::Checkin;
    checkins.schedule(tS, random(-node::CHECKIN_JITTER_S, node::CHECKIN_JITTER_S + 1));
  } else {
    LOG("NODE,t_s=%lu,activity=%d,x_dm=%d,y_dm=%d,pos=%s,steps=%lu,baseline=%u,sample_gaps=%lu\n",
        (unsigned long)tS, motion.valid ? int(motion.activity) : -1,
        node::toInt16(position.x() * 10), node::toInt16(position.y() * 10),
        position.usingGps() ? "gps" : "pdr", (unsigned long)pdr.steps(),
        unsigned(baseline.done()), (unsigned long)sampleGaps);
    return;
  }

  // The first check-in carries the baseline, so commander and node agree on it.
  const bool sendHr = firstCheckin ? baseline.hrValid() : hrValid;
  const bool sendTemp = firstCheckin ? baseline.tempValid() : tempValid;
  packet.soldierId = SOLDIER_ID;
  packet.seq = ++seq;
  packet.tS = static_cast<uint16_t>(tS);
  packet.code = code;
  packet.hrBpm = sendHr ? node::toByte(firstCheckin ? baseline.hrBpm() : hr) : node::HR_UNAVAILABLE;
  packet.skinTempCx100 = sendTemp ? node::toInt16((firstCheckin ? baseline.skinTempC() : temp) * 100)
                                  : node::TEMP_UNAVAILABLE;
  packet.activity = motion.valid ? node::toByte(motion.activity) : 0;
  const bool positionValid = position.usingGps() || (headingValid && imu.ok());
  packet.xDm = positionValid ? node::toPosition(position.x() * 10) : node::POSITION_UNAVAILABLE;
  packet.yDm = positionValid ? node::toPosition(position.y() * 10) : node::POSITION_UNAVAILABLE;
  packet.gas = 0;   // no gas sensor fitted
  packet.hops = 0;  // originated here; relaying is not implemented

  uint8_t bytes[node::PACKET_SIZE];
  node::encode(packet, bytes);
  radio.send(bytes);
  LOG("TX,t_s=%lu,type=%u,code=0x%02X,seq=%u,hr=%u,temp_c_x100=%d,x_dm=%d,y_dm=%d\n",
      (unsigned long)tS, unsigned(packet.type), unsigned(packet.code), packet.seq,
      packet.hrBpm, packet.skinTempCx100, packet.xDm, packet.yDm);
}
}  // namespace

void setup() {
  statusLed.begin();
  Serial.begin(SERIAL_DEBUG_BAUD);
  beginLogging();
  radioReady = radio.begin();
  randomSeed(radio.addressHash() ^ micros());
  seq = static_cast<uint16_t>(random(0, 65536));  // a reboot does not reuse recent (id, seq) pairs
  if (CFG_ANALOG) analogReady = analog.begin();
  imu.begin();
  if (CFG_GPS) gps.begin();
#if defined(AO_ORIGIN_LAT_E7)
  position.setOrigin(AO_ORIGIN_LAT_E7, AO_ORIGIN_LON_E7);
#endif
  readyMs = millis();
  LOG("[main] ready soldier=%u imu=%u analog=%u radio=%u\n", unsigned(SOLDIER_ID), imu.ok(), analogReady, radioReady);
}

void loop() {
  PulseTrace::poll(millis());
  if (CFG_GPS) gps.poll();
  if (retryTick.due(uint32_t(millis() * 1000U)) && !imu.active()) imu.begin();
  imu.poll();
  if (imuTick.due(uint32_t(millis() * 1000U))) {
    ImuPacket raw{};
    OrientationPacket orientation{};
    if (imu.read(raw, orientation)) {
      activity.add(raw.ax, raw.ay, raw.az);
      const float magnitude = node::accelMagnitudeG(raw.ax, raw.ay, raw.az, ACCEL_G_PER_LSB);
      float heading = 0;
      if (imu.hasOrientation()) {
        heading = node::headingFromQuaternion(orientation); headingValid = true;
      } else {
        headingValid = rawHeading.update(raw.t_ms, magnitude, raw.gx * GYRO_DPS_PER_LSB,
                                        raw.gy * GYRO_DPS_PER_LSB, raw.gz * GYRO_DPS_PER_LSB);
        heading = rawHeading.headingRad();
      }
      if (headingValid) pdr.update(raw.t_ms, magnitude, heading);
      static Periodic serialImu(1000000);
      if (serialImu.due(uint32_t(millis() * 1000U))) {
        SerialTelemetry::print(raw);
        if (imu.hasOrientation()) SerialTelemetry::print(orientation);
      }
    }
  }
  if (CFG_ANALOG && vitalsTick.due(uint32_t(millis() * 1000U))) {
    if (analog.snapshot(vitals, sampleGaps)) {
      static Periodic serialVitals(1000000);
      if (serialVitals.due(uint32_t(millis() * 1000U))) SerialTelemetry::print(vitals);
    }
  }
  if (nodeTick.due(uint32_t(millis() * 1000U))) {
    if (CFG_GPS) {
      GpsPacket packet{};
      gps.snapshot(packet);
      SerialTelemetry::print(packet);
    }
    LOG("SENSORS,model=%u,imu=%u,tmp36=%u,pulse=%u,gps=%u,samples=%lu,read_errors=%lu,starts=%lu\n",
        unsigned(CFG_IMU_MODEL), imu.ok(), unsigned(CFG_TMP36), unsigned(CFG_PULSE), unsigned(CFG_GPS),
        (unsigned long)imu.samples(), (unsigned long)imu.readErrors(), (unsigned long)imu.restarts());
    nodeStep(millis());
  }
  if (radioReady) radio.update(millis());

  const uint32_t nowMs = millis();
  const bool settled = (uint32_t)(nowMs - readyMs) >= 1000;
  const bool analogStale = settled && (uint32_t)(nowMs - vitals.t_ms) >= 1000;
  StatusLed::State ledState;
  if (!radioReady || (CFG_ANALOG && (!analogReady || analogStale))) ledState = StatusLed::State::Fatal;
  else if (imu.active() && !imu.ok()) ledState = StatusLed::State::Boot;
  else if (!imu.ok() || (CFG_TMP36 && settled && vitals.temp_c_x100 == INT16_MIN))
    ledState = StatusLed::State::SensorFault;
  else ledState = checkins.started() ? StatusLed::State::Reporting : StatusLed::State::Baseline;
  statusLed.update(ledState, nowMs);
  delay(MAIN_IDLE_MS);
}
