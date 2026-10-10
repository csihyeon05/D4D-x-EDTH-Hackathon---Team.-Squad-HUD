#include "i2c_hardware.h"
#include "core/log.h"
#ifdef BOARD_AMB26
extern "C" {
#include "PinNames.h"
#include "i2c_api.h"
}
namespace HardwareI2c {
namespace { i2c_t peripheral{}; bool active = false; uint8_t target = 0xFF; bool failed = true; }
void end() {
  if (!active) return;
  i2c_reset(&peripheral);
  Pinmux_Config(PB_21, PINMUX_FUNCTION_GPIO);
  Pinmux_Config(PB_20, PINMUX_FUNCTION_GPIO);
  active = false;
}
bool begin(uint8_t sda, uint8_t scl) {
  if (sda != 18 || scl != 19) return false;
  i2c_init(&peripheral, PB_21, PB_20);
  i2c_frequency(&peripheral, 100000);
  active = true; target = 0xFF; failed = true;
  delay(2);
  // Establish the first target with an address-only transaction before a
  // combined register read; this follows the verified hardware diagnostic.
  auto* hw = peripheral.USI_I2Cx;
  for (uint8_t address = 0x08; address <= 0x6B; ++address) {
    delayMicroseconds(100);
    USI_I2C_Cmd(hw, DISABLE);
    USI_I2C_SetSlaveAddress(hw, address);
    hw->I2C_CTRL |= USI_I2C_RESTART_EN;
    USI_I2C_ClearAllINT(hw);
    USI_I2C_Cmd(hw, ENABLE);
    uint8_t byte = address << 1;
    USI_I2C_MasterSendNullData(hw, &byte, 0, 1, 0);
    const uint32_t since = millis();
    bool ack = false;
    while (uint32_t(millis()-since) < 20) {
      const uint32_t flags = USI_I2C_GetRawINT(hw);
      if (flags & USI_I2C_TX_ABRT_RSTS) break;
      if (flags & USI_I2C_STOP_DET_RSTS) { ack = true; break; }
      delay(1);
    }
    if (ack && (address == 0x6A || address == 0x6B)) {
      target = address; failed = false;
      LOG("[i2c-hw] initial target=%02X\n", address);
      break;
    }
  }
  return true;
}
Result transfer(uint8_t address, uint8_t reg, uint8_t* data, uint8_t count,
                bool read, bool probe, uint8_t budget) {
  Result result{false,0,0,false,0};
  if (!active || address > 0x7F || (!probe && (!data || count == 0 || count > 16))) return result;
  auto* hw = peripheral.USI_I2Cx;
  const uint32_t started = millis();
  // Allow the abort trap to settle before clearing/restarting the peripheral.
  delayMicroseconds(100);
  if (target != address || failed) {
    USI_I2C_Cmd(hw, DISABLE);
    USI_I2C_SetSlaveAddress(hw, address);
    hw->I2C_CTRL |= USI_I2C_RESTART_EN;
    USI_I2C_ClearAllINT(hw);
    USI_I2C_Cmd(hw, ENABLE);
    target = address;
  } else USI_I2C_ClearAllINT(hw);
  auto expired = [&]() { return uint32_t(millis() - started) >= budget; };
  auto aborted = [&]() { return (USI_I2C_GetRawINT(hw) & USI_I2C_TX_ABRT_RSTS) != 0; };
  auto enqueue = [&](uint32_t command) {
    while ((hw->TX_FIFO_STATUS & USI_TXFIFO_FULL) != 0) {
      if (expired() || aborted()) return false;
      delay(1);
    }
    if (expired() || aborted()) return false;
    hw->TX_FIFO_WRITE = command;
    return true;
  };
  bool queued = false;
  if (probe) queued = enqueue((uint32_t(address) << 1) | (1U << 11) | (1U << 9));
  else {
    // USI emits RESTART after the marked byte, not before the next command.
    queued = enqueue(uint32_t(reg) | (read ? (1U << 10) : 0));
    for (uint8_t i = 0; queued && i < count; ++i) {
      const uint32_t command = read ? (1U << 8) : data[i];
      queued = enqueue(command | (i + 1 == count ? (1U << 9) : 0));
    }
  }
  while (queued && !expired() && !aborted()) {
    if (read && !probe) {
      while (result.received < count && (hw->RX_FIFO_STATUS & USI_RXFIFO_EMPTY) == 0)
        data[result.received++] = uint8_t(hw->RX_FIFO_READ);
    }
    if ((USI_I2C_GetRawINT(hw) & USI_I2C_STOP_DET_RSTS) &&
        (!read || probe || result.received == count)) { result.ok = true; break; }
    delay(1);
  }
  result.elapsed = millis() - started;
  result.abortSource = hw->I2C_TX_ABRT_SOURCE;
  result.timeout = !result.ok && expired();
  failed = !result.ok;
  if (failed) USI_I2C_Cmd(hw, DISABLE);
  return result;
}
}
#endif
