#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <initializer_list>
#include "sensors/imu.h"
#include "comm/i2c_bus.h"
static uint32_t now;
static uint8_t regs[256], address, sda;
static bool ioError, badReadback, stuckReset;
uint32_t millis() { return now; }
int digitalRead(uint8_t) { return 1; }
void logMessage(const char*, ...) {}
void I2cBus::begin() {}
bool I2cBus::probe(uint8_t a) { return !ioError && a == address && sdaPin() == sda; }
bool I2cBus::write(uint8_t a, uint8_t r, uint8_t v) {
  if (!probe(a)) return false;
  regs[r] = r == 0x12 && v == 1 && !stuckReset ? 0 : v;
  return true;
}
bool I2cBus::read(uint8_t a, uint8_t r, uint8_t* p, uint8_t n) {
  if (!probe(a)) return false;
  memcpy(p, regs+r, n);
  if (badReadback && r == 0x10) p[0] ^= 1;
  if (r == 0x22) regs[0x1E] = 0;
  return true;
}
static void reset(uint8_t id=0x69, uint8_t addr=0x6A, bool reverse=false) {
  memset(regs,0,sizeof(regs)); regs[0x0F]=id; address=addr;
  sda=reverse ? PIN_WIRE_SCL : PIN_WIRE_SDA;
  ioError=badReadback=stuckReset=false; now=0;
}
static void advance(Imu& imu, uint32_t amount) { now+=amount; imu.poll(); }
static void ready(Imu& imu) {
  assert(imu.begin()); advance(imu,10);
  for (int i=0;i<8;++i) advance(imu,5);
  advance(imu,150); assert(imu.ok());
  assert(regs[0x12]==0x44 && regs[0x10]==0x38 && regs[0x11]==0x34);
  assert(!imu.hasOrientation());
}
int main() {
  for (uint8_t id : {uint8_t(0x69),uint8_t(0x6A)}) {
    for (uint8_t addr : {uint8_t(0x6A),uint8_t(0x6B)}) {
      for (bool reverse : {false,true}) {
        reset(id,addr,reverse); Imu imu; ready(imu);
        ImuPacket packet{}; OrientationPacket q{};
        assert(!imu.read(packet,q));
        const uint8_t bytes[12]={0,0x80,0xFF,0x7F,0xFF,0xFF,1,0,0,0x20,0,0xE0};
        memcpy(regs+0x22,bytes,12);
        regs[0x1E]=1; advance(imu,10); assert(imu.samples()==0);
        regs[0x1E]=3; advance(imu,10); assert(imu.read(packet,q));
        assert(packet.ax==1 && packet.ay==8192 && packet.az==-8192);
        assert(packet.gx==-32768 && packet.gy==32767 && packet.gz==-1);
        assert(!imu.read(packet,q) && imu.samples()==1);
        ioError=true;
        for(int i=0;i<3;++i) advance(imu,10);
        assert(!imu.active() && imu.readErrors()==3);
        ioError=false; ready(imu); assert(imu.restarts()==2);
        advance(imu,1001); assert(!imu.active());
      }
    }
  }
  reset(0x68); Imu unknown; assert(!unknown.begin());
  reset(); stuckReset=true; Imu stuck; assert(stuck.begin());
  advance(stuck,501); assert(!stuck.active());
  reset(); badReadback=true; Imu mismatch; assert(mismatch.begin());
  for(int i=0;i<20;++i) advance(mismatch,10);
  assert(!mismatch.active());
  // Losing configuration or the bus during settling must not publish readiness.
  for (bool busFailure : {false, true}) {
    reset(); Imu settling; assert(settling.begin()); advance(settling,10);
    for (int i=0;i<8;++i) advance(settling,5);
    if (busFailure) ioError=true; else regs[0x11]=0;
    advance(settling,150); assert(!settling.ok());
    advance(settling,5); advance(settling,5);
    assert(!settling.active() && settling.readErrors()==3);
    ioError=false; ready(settling); assert(settling.restarts()==2);
  }
  reset(); now=UINT32_MAX-100; Imu wrap; ready(wrap);
  advance(wrap,100); assert(wrap.ok()); advance(wrap,1001); assert(!wrap.active());
  puts("LSM6DS3 driver tests passed");
}
