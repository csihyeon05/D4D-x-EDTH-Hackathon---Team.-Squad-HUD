#include <assert.h>
#include <deque>
#include <vector>
#include <stdio.h>
#include "comm/i2c_hardware.h"
extern "C" {
#include "i2c_api.h"
}
static USI_TypeDef hw;
static uint32_t now, flags;
static bool nack, noReceive, noStop, enabled;
static unsigned disables;
static uint16_t selected;
void logMessage(const char*, ...) {}
static std::deque<uint8_t> rx;
static std::vector<uint32_t> commands;
uint32_t millis() { return now; }
void delay(uint32_t ms) { now+=ms; }
void delayMicroseconds(uint32_t) {}
TxRegister& TxRegister::operator=(uint32_t command) {
 assert(enabled); commands.push_back(command);
 if(nack || selected != 0x6A) { flags |= USI_I2C_TX_ABRT_RSTS; hw.I2C_TX_ABRT_SOURCE=1; return *this; }
 if ((command & (1U<<8)) && !(command & (1U<<11)) && !noReceive) rx.push_back(uint8_t(rx.size()+10));
 if ((command & (1U<<9)) && !noStop) flags |= USI_I2C_STOP_DET_RSTS;
 return *this;
}
RxStatus::operator uint32_t() const { return rx.empty() ? USI_RXFIFO_EMPTY : 0; }
RxRegister::operator uint32_t() { assert(!rx.empty()); auto value=rx.front();rx.pop_front();return value; }
extern "C" {
void i2c_init(i2c_t* p,int sda,int scl) { assert(sda==53 && scl==52);p->USI_I2Cx=&hw; }
void i2c_frequency(i2c_t*,int hz) { assert(hz==100000); }
void i2c_reset(i2c_t*) { enabled=false; }
void Pinmux_Config(int,int) {}
void USI_I2C_Cmd(USI_TypeDef*,int value) { enabled=value; if(!enabled) { ++disables; rx.clear(); } }
void USI_I2C_SetSlaveAddress(USI_TypeDef*,uint16_t a) { assert(a>=8 && a<=0x77);selected=a; }
void USI_I2C_MasterSendNullData(USI_TypeDef* p,uint8_t* b,uint8_t cmd,uint8_t stop,uint8_t restart) { p->TX_FIFO_WRITE=*b | (1U<<11) | (uint32_t(cmd)<<8) | (uint32_t(stop)<<9) | (uint32_t(restart)<<10); }
void USI_I2C_ClearAllINT(USI_TypeDef*) { flags=0;hw.I2C_TX_ABRT_SOURCE=0; }
uint32_t USI_I2C_GetRawINT(USI_TypeDef*) { return flags; }
}
static void reset() {
 HardwareI2c::end(); now=flags=0;nack=noReceive=noStop=false;hw.TX_FIFO_STATUS=0;commands.clear();rx.clear();
 assert(HardwareI2c::begin(18,19));commands.clear();now=0;
}
int main() {
 uint8_t buffer[16]{};
 reset();auto r=HardwareI2c::transfer(0x6A,0, nullptr,0,false,true,20);
 assert(r.ok && commands.size()==1 && (commands[0] & (1U<<11)));
 reset();r=HardwareI2c::transfer(0x6A,0x22,buffer,12,true,false,50);
 assert(r.ok && r.received==12 && commands.size()==13);
 assert(commands[0]==(0x22 | (1U<<10)) && (hw.I2C_CTRL & USI_I2C_RESTART_EN));
 for(int i=0;i<12;++i) { assert(buffer[i]==i+10); assert(commands[i+1]==((1U<<8) | (i==11 ? (1U<<9) : 0))); }
 const unsigned before=disables;
 r=HardwareI2c::transfer(0x6A,0x22,buffer,12,true,false,50);
 assert(r.ok && disables==before && r.received==12);
 reset();buffer[0]=0x38;r=HardwareI2c::transfer(0x6A,0x10,buffer,1,false,false,20);
 assert(r.ok && commands[0]==0x10 && commands[1]==(0x38 | (1U<<9)));
 reset();nack=true;r=HardwareI2c::transfer(0x6A,0x22,buffer,12,true,false,50);
 assert(!r.ok && r.abortSource==1 && !r.timeout && !enabled);
 nack=false;r=HardwareI2c::transfer(0x6A,0x22,buffer,12,true,false,50);
 assert(r.ok && r.received==12);
 reset();noReceive=true;r=HardwareI2c::transfer(0x6A,0x22,buffer,12,true,false,50);
 assert(!r.ok && r.timeout && r.elapsed==50);
 reset();noStop=true;r=HardwareI2c::transfer(0x6A,0x10,buffer,1,false,false,20);
 assert(!r.ok && r.timeout && r.elapsed==20);
 reset();hw.TX_FIFO_STATUS=USI_TXFIFO_FULL;now=UINT32_MAX-5;
 r=HardwareI2c::transfer(0x6A,0x22,buffer,12,true,false,50);
 assert(!r.ok && r.timeout && r.elapsed==50 && commands.empty());
 HardwareI2c::end();assert(!HardwareI2c::begin(19,18));
 puts("Hardware I2C transaction tests passed");
}
