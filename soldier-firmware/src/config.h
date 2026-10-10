#pragma once
// Central configuration for board pins, wiring, and timing.

#include <Arduino.h>

// ---- Sensor profile ----
// 1 = MPU6050, 2 = NU-IMU LSM6DS3 family.
#ifndef CFG_IMU_MODEL
#define CFG_IMU_MODEL 1
#endif
static_assert(CFG_IMU_MODEL == 1 || CFG_IMU_MODEL == 2, "Unsupported IMU model");
#ifndef CFG_IMU_DMP
#define CFG_IMU_DMP 0
#endif
#ifndef CFG_TMP36
#define CFG_TMP36 1
#endif
#ifndef CFG_ANALOG
#define CFG_ANALOG (CFG_TMP36 || CFG_PULSE)
#endif
#ifndef CFG_PULSE
#define CFG_PULSE 1
#endif

constexpr float ACCEL_G_PER_LSB = CFG_IMU_MODEL == 2 ? 0.000122f : 1.0f / 16384.0f;
constexpr float GYRO_DPS_PER_LSB = CFG_IMU_MODEL == 2 ? 0.0175f : 1.0f / 16.4f;

// ---- Pins ----
// Analog inputs for TMP36GT9Z (Vout) and Pulse Sensor (Signal).
constexpr uint8_t PIN_TMP36 = A4;
constexpr uint8_t PIN_PULSE = A5;
// GPS UART (Serial1): GPS TX -> board RX, GPS RX -> board TX; pins follow the board variant.
// Preserve each board's verified wiring and I2C implementation.
#if CFG_IMU_MODEL == 1
constexpr uint8_t PIN_WIRE_SDA = 19;
constexpr uint8_t PIN_WIRE_SCL = 18;
constexpr char IMU_SDA_LABEL[] = "PB20";
constexpr char IMU_SCL_LABEL[] = "PB21";
#else
constexpr uint8_t PIN_WIRE_SDA = 18;
constexpr uint8_t PIN_WIRE_SCL = 19;
constexpr char IMU_SDA_LABEL[] = "PB21";
constexpr char IMU_SCL_LABEL[] = "PB20";
#endif
// NU87-TinyDK schematic: active-high RGB through SN74LVC3G17 buffers.
constexpr uint8_t STATUS_LED_RED = 2;   // PA13
constexpr uint8_t STATUS_LED_GREEN = 3; // PA12
constexpr uint8_t STATUS_LED_BLUE = 1;  // PA14

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
constexpr uint32_t VITALS_PERIOD_US = 200000;  // 5 Hz (temperature and pulse snapshots)
constexpr uint32_t NODE_PERIOD_US   = 1000000; // 1 Hz (alert rules, position, check-in)
constexpr uint32_t MAIN_IDLE_MS = 5;

// ---- Node identity (docs/DATA_SPEC.md) ----
// Set per board at build time: SOLDIER_ID=3 sh tools/nu87.sh build
#ifndef SOLDIER_ID
#error "Define SOLDIER_ID (1..255) for this board, e.g. SOLDIER_ID=3 sh tools/nu87.sh build"
#endif
static_assert(SOLDIER_ID >= 1 && SOLDIER_ID <= 255, "SOLDIER_ID must be 1..255");
// Optional shared area-of-operations origin, degrees x 1e7. When set, x/y are
// metres east/north of it once GPS has a fix; otherwise of the power-on position.
#if defined(AO_ORIGIN_LAT_E7) != defined(AO_ORIGIN_LON_E7)
#error "Define both AO_ORIGIN_LAT_E7 and AO_ORIGIN_LON_E7, or neither"
#endif

// ---- Radio: continuous BLE advertising, refreshed once per second ----
constexpr uint16_t ADV_INTERVAL_MIN_MS = 100; // Conservative legacy non-connectable interval
constexpr uint16_t ADV_INTERVAL_MAX_MS = 120;

// GPS comes from the phone over Wi-Fi to the backend. UART GPS is optional.
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
