#pragma once
#include <stdint.h>
#include "node/packet.h"

// Dedicated non-connectable broadcaster. Updates data without stopping BLE.
class Advertiser {
public:
  bool begin();
  void send(const uint8_t packet[node::PACKET_SIZE]);
  void update(uint32_t nowMs);
  void logStatus(uint32_t nowMs) const;
  uint32_t sent() const { return completed_; } // Controller updates, NOT receiver acknowledgements.
  uint32_t addressHash() const;
private:
  uint8_t advert_[node::ADVERT_SIZE] = {};
  bool pending_ = false, inFlight_ = false, timedOut_ = false;
  uint32_t queued_ = 0, accepted_ = 0, completed_ = 0, failures_ = 0;
  uint32_t timeouts_ = 0, starts_ = 0, requestedMs_ = 0, lastAttemptMs_ = 0;
  uint32_t completedMs_ = 0;
  uint16_t cause_ = 0;
  uint8_t gapState_ = 255;
};
