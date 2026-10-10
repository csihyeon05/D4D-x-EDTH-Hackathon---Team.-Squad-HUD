#pragma once
// Central configuration for board pins, wiring, and timing.

#include <Arduino.h>

// ---- Pins ----
// Analog inputs for TMP36GT9Z (Vout) and Pulse Sensor (Signal).
constexpr uint8_t PIN_TMP36 = A4;
constexpr uint8_t PIN_PULSE = A5;
// GPS UART (Serial1): GPS TX -> board RX, GPS RX -> board TX; pins follow the board variant.
// MPU6050: SDA=PB20, SCL=PB21 (both directions probed); TMP36=PB1, Pulse=PB2.
constexpr uint8_t PIN_WIRE_SDA = 19;
constexpr uint8_t PIN_WIRE_SCL = 18;
constexpr char IMU_SDA_LABEL[] = "PB20";
constexpr char IMU_SCL_LABEL[] = "PB21";
// NU87-TinyDK schematic: active-high RGB through SN74LVC3G17 buffers.
constexpr uint8_t STATUS_LED_RED = 2;   // PA13
constexpr uint8_t STATUS_LED_GREEN = 3; // PA12
constexpr uint8_t STATUS_LED_BLUE = 1;  // PA14

// Raw register mode is the default while DMP communication is being diagnosed.
#ifndef CFG_IMU_DMP
#define CFG_IMU_DMP 0
#endif

// ---- GPS ----
constexpr uint32_t GPS_BAUD = 9600;  // Actual module baud rate must match.

static_assert(PIN_TMP36 != PIN_PULSE, "Analog sensors need separate pins");

// ---- ADC ----
constexpr uint8_t  ADC_BITS      = 12;
constexpr uint16_t ADC_VREF_MV   = 3300;  // Ameba analogRead returns calibrated millivolts scaled by 2^resolution.
constexpr uint32_t ADC_SCALE = 1UL << ADC_BITS;

static_assert(ADC_BITS >= 10 && ADC_BITS <= 12, "Supported ADC resolution: 10..12");

// ---- Sampling periods (us) ----
constexpr uint32_t PULSE_PERIOD_US = 2000;     // 500 Hz (beat detection)
constexpr uint32_t IMU_PERIOD_US   = 20000;    // 50 Hz
constexpr uint32_t VITALS_PERIOD_US = 200000;  // 5 Hz (temperature and pulse telemetry)
constexpr uint32_t GPS_PERIOD_US    = 1000000; // 1 Hz (latest GPS telemetry)

// ---- BLE ----
constexpr char BLE_DEVICE_NAME[] = "NU87-1";
constexpr uint8_t BLE_NODE_ID = 1;
constexpr char IMU_MODEL_NAME[] = "mpu6050";
// Sensor acquisition remains fast; NUS publishes bounded-rate snapshots.
constexpr uint32_t BLE_SENSOR_PERIOD_MS = 200;
constexpr uint32_t BLE_CHUNK_PERIOD_MS = 10;
constexpr uint32_t MAIN_IDLE_MS = 5;
// GPS is not fitted; enable when a compatible UART NMEA module is connected.
#ifndef CFG_GPS
#define CFG_GPS 0
#endif
constexpr uint32_t GPS_STALE_MS = 3000;
constexpr uint32_t IMU_RETRY_US = 2000000;

// ---- Debug logging (USB-UART Serial; separate from GPS Serial1) ----
constexpr uint32_t SERIAL_DEBUG_BAUD = 9600;
#ifndef CFG_DEBUG
#define CFG_DEBUG 1
#endif
