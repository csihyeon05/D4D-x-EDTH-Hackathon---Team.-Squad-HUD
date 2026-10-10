#include "ble_link.h"
#include <BLEDevice.h>
#include <atomic>
#include "config.h"
#include "core/log.h"
#include "protocol/uuids.h"
#include "protocol/nus_json.h"
#include "nus_queue.h"

namespace {
BLEService service(UUID_NUS_SERVICE);
BLECharacteristic rx(UUID_NUS_RX), tx(UUID_NUS_TX);
BLEAdvertData advertisement, scan;
std::atomic<int8_t> pendingSubscription{-1};
bool subscribed = false, wasConnected = false, boundary = false;
NusQueue queue;
uint32_t sequence = 0, failures = 0, requests = 0, lastTx = 0;
uint32_t lastQueued[5] = {};
bool queued[5] = {};
void subscription(BLECharacteristic*, uint8_t connection, uint16_t bits) {
  if (connection == 0)
    pendingSubscription.store((bits & GATT_CLIENT_CHAR_CONFIG_NOTIFY) ? 1 : 0);
}
bool ready(unsigned kind) {
  if (!BLE.connected(0) || !subscribed) return false;
  const uint32_t now = millis();
  const uint32_t period = (kind == 2 || kind == 3) ? 1000 : BLE_SENSOR_PERIOD_MS;
  if (queued[kind] && uint32_t(now - lastQueued[kind]) < period) return false;
  return true;
}
bool put(unsigned kind, const char* line, size_t size) {
  if (!queue.put(kind, line, size)) return false;
  ++sequence; queued[kind] = true; lastQueued[kind] = millis(); return true;
}
void clearSession() {
  queue.clear();
  for (auto& sent : queued) sent = false;
  subscribed = false; boundary = false;
}
}
bool BleLink::begin() {
  static_assert(sizeof(BLE_DEVICE_NAME) - 1 <= 8, "Primary advertisement name exceeds 31-byte budget");
  advertisement.addFlags(GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED);
  advertisement.addCompleteServices(BLEUUID(UUID_NUS_SERVICE));
  advertisement.addCompleteName(BLE_DEVICE_NAME);
  scan.addCompleteName(BLE_DEVICE_NAME);
  rx.setWriteProperty(true);
  rx.setWritePermissions(GATT_PERM_WRITE);
  rx.setBufferLen(20);
  tx.setReadProperty(true);
  tx.setReadPermissions(GATT_PERM_READ);
  tx.setNotifyProperty(true);
  tx.setCCCDCallback(subscription);
  tx.setBufferLen(20);
  service.addCharacteristic(rx); service.addCharacteristic(tx);
  BLE.init(); BLE.setDeviceName(BLE_DEVICE_NAME);
  BLE.configAdvert()->setAdvData(advertisement);
  BLE.configAdvert()->setScanRspData(scan);
  BLE.configServer(1); BLE.addService(service); BLE.beginPeripheral();
  LOG("[ble] %s NUS ready; sensor telemetry=%lu ms\n",BLE_DEVICE_NAME,(unsigned long)BLE_SENSOR_PERIOD_MS);
  return true; // The vendor initialization API has no success return value.
}
void BleLink::poll() {
  const bool connectedNow = BLE.connected(0);
  if (!connectedNow) {
    pendingSubscription.exchange(-1); clearSession();
  } else {
    const int8_t event = pendingSubscription.exchange(-1);
    if (event >= 0) {
      clearSession(); subscribed = event != 0; boundary = subscribed;
      LOG("[ble] notify=%u\n",subscribed);
    }
  }
  if (connectedNow != wasConnected) {
    LOG("[ble] %s %s\n",BLE_DEVICE_NAME,connectedNow ? "connected" : "disconnected");
    wasConnected = connectedNow;
  }
  const uint32_t now = millis();
  if (!connectedNow || !subscribed || uint32_t(now-lastTx) < BLE_CHUNK_PERIOD_MS) return;
  char chunk[21];
  const size_t count = boundary ? (chunk[0]='\n',chunk[1]=0,1) : queue.front(chunk);
  if (!count) return;
  lastTx = now;
  if (!tx.writeString(chunk)) { ++failures; return; }
  // notify() has no delivery result. Requests do not prove radio reception.
  tx.notify(0); ++requests;
  if (boundary) boundary = false; else queue.commit(count);
}
bool BleLink::connected() const { return BLE.connected(0); }
uint32_t BleLink::dropped() const { return failures + queue.dropped(); }
uint32_t BleLink::txRequests() const { return requests; }
bool BleLink::sendImu(const ImuPacket& p) {
  if (!ready(0)) return false;
  char line[NusQueue::CAPACITY];
  const size_t size = nus::encode(line,sizeof(line),BLE_NODE_ID,sequence,IMU_MODEL_NAME,p);
  return put(0,line,size);
}
#define SEND_PACKET(Method, Type, Index) \
  bool BleLink::Method(const Type& p) { \
    if (!ready(Index)) return false; \
    char line[NusQueue::CAPACITY]; \
    const size_t size = nus::encode(line,sizeof(line),BLE_NODE_ID,sequence,p); \
    return put(Index,line,size); \
  }
SEND_PACKET(sendVitals, VitalsPacket, 1)
SEND_PACKET(sendGps, GpsPacket, 2)
SEND_PACKET(sendStatus, StatusPacket, 3)
SEND_PACKET(sendOrientation, OrientationPacket, 4)
