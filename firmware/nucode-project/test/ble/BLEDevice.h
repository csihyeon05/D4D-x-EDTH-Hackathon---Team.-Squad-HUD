#pragma once
#include <stdint.h>
#include <string>
#include <vector>
constexpr int GAP_ADTYPE_FLAGS_GENERAL=2, GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED=4;
constexpr int GATT_PERM_WRITE=2, GATT_PERM_READ=1, GATT_CLIENT_CHAR_CONFIG_NOTIFY=1;
struct BLEUUID { explicit BLEUUID(const char*) {} };
struct BLEAdvertData {
  unsigned size=0;
  void addFlags(int) { size+=3; }
  void addCompleteServices(BLEUUID) { size+=18; }
  void addCompleteName(const char* s) { size+=std::string(s).size()+2; }
};
struct BLECharacteristic;
extern BLECharacteristic* txCharacteristic;
extern bool failWrite;
extern std::vector<std::string> notifications;
struct BLECharacteristic {
  using Callback=void(*)(BLECharacteristic*,uint8_t,uint16_t);
  Callback callback=nullptr;
  unsigned buffer=0; bool writable=false,readable=false; std::string data;
  explicit BLECharacteristic(const char*) {}
  void setWriteProperty(bool v) { writable=v; }
  void setWritePermissions(int) {}
  void setReadProperty(bool v) { readable=v; }
  void setReadPermissions(int) {}
  void setNotifyProperty(bool v) { if(v) txCharacteristic=this; }
  void setCCCDCallback(Callback c) { callback=c; }
  void setBufferLen(unsigned n) { buffer=n; }
  bool writeString(const char* s) { if(failWrite) return false; data=s; return data.size()<=buffer; }
  void notify(int) { notifications.push_back(data); }
};
struct BLEService {
  explicit BLEService(const char*) {}
  void addCharacteristic(BLECharacteristic&) {}
};
struct Radio {
  bool online=false; unsigned advertSize=0, services=0;
  bool connected(int) { return online; }
  void init() {}
  void setDeviceName(const char*) {}
  Radio* configAdvert() { return this; }
  void setAdvData(BLEAdvertData& a) { advertSize=a.size; }
  void setScanRspData(BLEAdvertData&) {}
  void configServer(int) {}
  void addService(BLEService&) { ++services; }
  void beginPeripheral() {}
};
extern Radio BLE;
