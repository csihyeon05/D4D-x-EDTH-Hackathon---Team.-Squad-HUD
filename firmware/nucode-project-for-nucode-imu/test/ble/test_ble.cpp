#include "BLEDevice.h"
#include "comm/ble_link.h"
#include "config.h"
#include <assert.h>
#include <iostream>
Radio BLE;
BLECharacteristic* txCharacteristic=nullptr;
bool failWrite=false;
std::vector<std::string> notifications;
uint32_t nowMs=0;
uint32_t millis() { return nowMs; }
void logMessage(const char*,...) {}
void tick(BleLink& link,unsigned count=1) {
  while(count--) { nowMs+=10; link.poll(); }
}
int main() {
  BleLink link; assert(link.begin());
  assert(BLE.advertSize==29 && BLE.services==1);
  ImuPacket p{1,1,2,3,4,5,6};
  assert(!link.sendImu(p));
  BLE.online=true; tick(link); assert(!link.sendImu(p));
  txCharacteristic->callback(txCharacteristic,1,1); tick(link); assert(!link.sendImu(p));
  txCharacteristic->callback(txCharacteristic,0,1); tick(link);
  assert(notifications.size()==1 && notifications[0]=="\n");
  assert(link.sendImu(p)); assert(!link.sendImu(p));
  failWrite=true; tick(link); assert(link.dropped()==1 && notifications.size()==1);
  failWrite=false; tick(link,20);
  std::string line;
  for(size_t i=1;i<notifications.size();++i) { assert(notifications[i].size()<=20); line+=notifications[i]; }
  assert(line.find("\"type\":\"imu\"")!=std::string::npos && line.back()=='\n');
  assert(link.txRequests()==notifications.size());
  // Disconnect clears an unfinished line and requires a new subscription.
  assert(link.sendImu(p)); tick(link); BLE.online=false; tick(link);
  const size_t count=notifications.size(); BLE.online=true; tick(link,30);
  assert(notifications.size()==count && !link.sendImu(p));
  txCharacteristic->callback(txCharacteristic,0,1); tick(link);
  assert(notifications.back()=="\n" && link.sendImu(p));
  txCharacteristic->callback(txCharacteristic,0,0); tick(link);
  assert(!link.sendImu(p));
  std::cout<<"BLE subscription, pacing, failure and reconnect tests passed\n";
}
