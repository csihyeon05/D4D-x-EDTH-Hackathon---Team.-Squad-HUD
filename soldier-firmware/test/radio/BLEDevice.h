#pragma once
#include <stdint.h>
struct FakeAdvert {
 void setAdvType(int) {} void setMinInterval(int) {} void setMaxInterval(int) {}
 void setAdvData(uint8_t*,unsigned) {}
};
struct FakeBLE {
 FakeAdvert adv;
 void init() {} void beginPeripheral() {}
 FakeAdvert* configAdvert() { return &adv; }
};
extern FakeBLE BLE;
