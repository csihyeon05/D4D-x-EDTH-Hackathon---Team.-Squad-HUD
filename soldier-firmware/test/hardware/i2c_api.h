#pragma once
#include <stdint.h>
#define DISABLE 0
#define ENABLE 1
#define USI_I2C_RESTART_EN 2U
#define USI_I2C_STOP_DET_RSTS (1U << 24)
#define USI_I2C_TX_ABRT_RSTS (1U << 21)
#define USI_TXFIFO_FULL 1U
#define USI_RXFIFO_EMPTY 2U
struct TxRegister { TxRegister& operator=(uint32_t); };
struct RxStatus { operator uint32_t() const; };
struct RxRegister { operator uint32_t(); };
struct USI_TypeDef {
 uint32_t I2C_CTRL=0, TX_FIFO_STATUS=0, I2C_TX_ABRT_SOURCE=0;
 TxRegister TX_FIFO_WRITE;
 RxStatus RX_FIFO_STATUS;
 RxRegister RX_FIFO_READ;
};
struct i2c_t { USI_TypeDef* USI_I2Cx; };
void i2c_init(i2c_t*, int, int);
void i2c_frequency(i2c_t*, int);
void i2c_reset(i2c_t*);
void Pinmux_Config(int,int);
void USI_I2C_Cmd(USI_TypeDef*, int);
void USI_I2C_SetSlaveAddress(USI_TypeDef*, uint16_t);
void USI_I2C_ClearAllINT(USI_TypeDef*);
uint32_t USI_I2C_GetRawINT(USI_TypeDef*);

void USI_I2C_MasterSendNullData(USI_TypeDef*,uint8_t*,uint8_t,uint8_t,uint8_t);
