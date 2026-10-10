#include "i2c_diagnostic.h"
#ifdef BOARD_AMB26
#include "core/log.h"
extern "C" {
#include "PinNames.h"
#include "i2c_api.h"
}
#include <initializer_list>

// Independent, bounded WHO_AM_I read using the USI hardware on PB21/PB20.
// The stock Wire wrapper rejects these pins despite SDK pinmux support.
void hardwareI2cDiagnostic() {
  i2c_t peripheral{};
  i2c_init(&peripheral, PB_21, PB_20);
  i2c_frequency(&peripheral, 100000);
  unsigned found = 0, timedOut = 0;
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    auto* hw = peripheral.USI_I2Cx;
    delayMicroseconds(100);
    USI_I2C_Cmd(hw, DISABLE);
    USI_I2C_SetSlaveAddress(hw, address);
    hw->I2C_CTRL |= USI_I2C_RESTART_EN;
    USI_I2C_ClearAllINT(hw);
    USI_I2C_Cmd(hw, ENABLE);
    uint8_t addressByte = address << 1;
    USI_I2C_MasterSendNullData(hw, &addressByte, 0, 1, 0);
    int probe = -2;
    const uint32_t probeStart = millis();
    while (uint32_t(millis() - probeStart) < 20) {
      const uint32_t status = USI_I2C_GetRawINT(hw);
      if (status & USI_I2C_TX_ABRT_RSTS) { probe = -1; break; }
      if (status & USI_I2C_STOP_DET_RSTS) { probe = 0; break; }
      delay(1);
    }
    if (probe == 0) ++found;
    if (probe == -2) ++timedOut;
    const uint32_t abortSource = hw->I2C_TX_ABRT_SOURCE;
    uint8_t id = 0;
    bool received = false;
    uint32_t flags = USI_I2C_GetRawINT(hw);
    if (probe == 0 && (address == 0x6A || address == 0x6B) && (flags & USI_I2C_STOP_DET_RSTS)) {
      USI_I2C_ClearAllINT(hw);
      // Register pointer followed by repeated START, one read byte and STOP.
      hw->TX_FIFO_WRITE = 0x0F | (1U << 10);
      hw->TX_FIFO_WRITE = (1U << 8) | (1U << 9);
      const uint32_t started = millis();
      while (uint32_t(millis() - started) < 20) {
        flags = USI_I2C_GetRawINT(hw);
        if (flags & USI_I2C_TX_ABRT_RSTS) break;
        if ((hw->RX_FIFO_STATUS & USI_RXFIFO_EMPTY) == 0) {
          id = uint8_t(hw->RX_FIFO_READ); received = true; break;
        }
        delay(1);
      }
    }
    if (probe == 0 || address == 0x6A || address == 0x6B) LOG("[imu-hw] SDA=PB21 SCL=PB20 addr=%02X probe=%d read=%u id=%02X flags=%08lX abort=%08lX\n",
        address, probe, received, id, (unsigned long)flags, (unsigned long)abortSource);
  }
  LOG("[imu-hw] scan complete addresses=112 found=%u timeouts=%u\n", found, timedOut);
  i2c_reset(&peripheral);
  // Restore pinmux explicitly: Arduino caches GPIO modes across this diagnostic.
  Pinmux_Config(PB_21, PINMUX_FUNCTION_GPIO);
  Pinmux_Config(PB_20, PINMUX_FUNCTION_GPIO);
}
#endif
