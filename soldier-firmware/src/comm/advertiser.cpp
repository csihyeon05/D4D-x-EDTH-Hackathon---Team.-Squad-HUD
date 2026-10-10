#include "advertiser.h"
#include <BLEDevice.h>
#include <atomic>
#include <string.h>
#include "config.h"
#include "core/log.h"
extern "C" {
#include "gap.h"
#include "gap_le.h"
#include "gap_adv.h"
#include "gap_callback_le.h"
#include "gap_msg.h"
}

namespace {
std::atomic<int32_t> completion{-1};
// This firmware only broadcasts non-connectable advertisements: no GATT,
// scanning or connection callbacks are used. Keep the vendor IO/state handler.
T_APP_RESULT advertisementCallback(uint8_t type, void* data) {
  if (type == GAP_MSG_LE_ADV_UPDATE_PARAM && data) {
    const auto* event = static_cast<T_LE_CB_DATA*>(data);
    completion.store(event->p_le_adv_update_param_rsp->cause);
  }
  return APP_RESULT_SUCCESS;
}
}

bool Advertiser::begin() {
  // Start with the final payload length, including explicit unavailable values.
  node::NodePacket boot{};
  boot.type = node::PacketType::Checkin;
  boot.soldierId = SOLDIER_ID;
  boot.skinTempCx100 = node::TEMP_UNAVAILABLE;
  boot.xDm = boot.yDm = node::POSITION_UNAVAILABLE;
  uint8_t initial[node::PACKET_SIZE];
  node::encode(boot, initial);
  node::buildAdvert(initial, advert_);
  BLE.init();
  BLE.configAdvert()->setAdvType(GAP_ADTYPE_ADV_NONCONN_IND);
  BLE.configAdvert()->setMinInterval(ADV_INTERVAL_MIN_MS);
  BLE.configAdvert()->setMaxInterval(ADV_INTERVAL_MAX_MS);
  BLE.configAdvert()->setAdvData(advert_, sizeof(advert_));
  BLE.beginPeripheral();
  completion.store(-1);
  le_register_app_cb(advertisementCallback);
  return true;
}

void Advertiser::send(const uint8_t packet[node::PACKET_SIZE]) {
  node::buildAdvert(packet, advert_);
  pending_ = true; // Coalesce delayed samples: only the latest measurement matters.
  ++queued_;
}

void Advertiser::update(uint32_t nowMs) {
  T_GAP_DEV_STATE state{};
  const auto stateCause = le_get_gap_param(GAP_PARAM_DEV_STATE, &state);
  if (stateCause != GAP_CAUSE_SUCCESS) { cause_ = stateCause; gapState_ = 255; return; }
  gapState_ = state.gap_adv_state;
  const int32_t result = completion.exchange(-1);
  if (inFlight_ && result >= 0) {
    inFlight_ = false;
    cause_ = static_cast<uint16_t>(result);
    if (result == GAP_CAUSE_SUCCESS) { ++completed_; completedMs_ = nowMs; }
    else { ++failures_; pending_ = true; }
  }
  if (inFlight_) {
    if (!timedOut_ && uint32_t(nowMs - requestedMs_) >= 2000) {
      ++timeouts_; timedOut_ = true;
    }
    // Do not overlap requests or misattribute a late callback to a new packet.
    return;
  }
  if (uint32_t(nowMs - lastAttemptMs_) < 250) return;
  if (gapState_ == GAP_ADV_STATE_IDLE) {
    if (uint32_t(nowMs - lastAttemptMs_) < 1000) return;
    lastAttemptMs_ = nowMs;
    cause_ = le_adv_start();
    ++starts_;
    if (cause_ != GAP_CAUSE_SUCCESS) ++failures_;
    return;
  }
  if (gapState_ != GAP_ADV_STATE_ADVERTISING || !pending_) return;
  lastAttemptMs_ = nowMs;
  // SDK permits only ADV_DATA / SCAN_RSP_DATA changes while advertising.
  // BLEAdvert::updateAdvertParams also sets idle-only parameters: do not use it.
  cause_ = le_adv_set_param(GAP_PARAM_ADV_DATA, sizeof(advert_), advert_);
  if (cause_ != GAP_CAUSE_SUCCESS) { ++failures_; return; }
  completion.store(-1);
  inFlight_ = true;
  timedOut_ = false;
  requestedMs_ = nowMs;
  cause_ = le_adv_update_param();
  if (cause_ != GAP_CAUSE_SUCCESS) { inFlight_ = false; ++failures_; return; }
  pending_ = false;
  ++accepted_;
}

void Advertiser::logStatus(uint32_t nowMs) const {
  LOG("RADIO,gap=%u,q=%lu,req=%lu,done=%lu,fail=%lu,timeout=%lu,start=%lu,wait=%u,cause=%u,age=%lu\n",
      unsigned(gapState_), (unsigned long)queued_, (unsigned long)accepted_,
      (unsigned long)completed_, (unsigned long)failures_, (unsigned long)timeouts_,
      (unsigned long)starts_, unsigned(inFlight_), unsigned(cause_),
      (unsigned long)(completed_ ? nowMs - completedMs_ : UINT32_MAX));
}

uint32_t Advertiser::addressHash() const {
  uint8_t address[6] = {};
  gap_get_param(GAP_PARAM_BD_ADDR, address);
  uint32_t hash = 2166136261u;
  for (uint8_t byte : address) hash = (hash ^ byte) * 16777619u;
  return hash;
}
