# NU-87 Tiny DK + NU-IMU Firmware

This copy targets NU-87 Tiny DK (RTL8720DF), official Realtek AmebaD 3.1.9, AMB26, and the NU-IMU LSM6DS3 family. IMU and TMP36 are enabled. `CFG_ANALOG=1` enables TMP36 acquisition, `CFG_PULSE=0` disables pulse acquisition, and `CFG_GPS=0` disables GPS. The original sibling project is separate.

## Wiring

Use 3.3V logic and a common ground. MCU names are not Arduino indices.

| NU-IMU signal | NU-87 signal | Arduino index | Header |
|---|---|---|---|
| VCC | 3.3V | - | 3.3V supply |
| GND | GND | - | Ground |
| SDA | PB21 | 18 | P2 pin 8 |
| SCL | PB20 | 19 | P2 pin 7 |

The driver uses hardware USI I2C on SDA=PB21 / SCL=PB20 and probes addresses 0x6A/0x6B. A reversed-pin software fallback is retained for diagnostics. It accepts WHO_AM_I 0x69 (LSM6DS3) or 0x6A (LSM6DS3TR-C), and logs the actual mapping. Keep I2C pull-ups at 3.3V. INT is unused. PA7/PA8 remain reserved for USB-UART; PA25/PA26 are native USB signals.

TMP36 wiring: VOUT to PB1/A4 (P2 pin 5), supply to 3.3V, and ground to common GND. The user confirmed this wiring. Temperature averages 16 readings taken at 100 Hz. Future Pulse signal goes to PB2/A5 (P2 pin 4); enable `CFG_PULSE=1` after wiring it. With pulse disabled, BPM and pulse raw fields are zero. Future GPS TX goes to PB18/Serial1 RX (P2 pin 9), optional GPS RX to PB19/Serial1 TX (P2 pin 10); enable `CFG_GPS` and match `GPS_BAUD`. These sensors are not validated by the IMU-only test.

## Build and Upload

```sh
sh tools/nu87.sh setup
sh tools/nu87.sh build
sh tools/nu87.sh upload /dev/ttyUSB0
sh tools/nu87.sh monitor /dev/ttyUSB0
sh tools/test.sh
```

Arduino CLI must be on PATH or at `~/.local/bin/arduino-cli`. `ARDUINO_CLI` can override it. PlatformIO has no active environment for this Realtek target. Close serial monitors before uploading.

The build script stages sources in a private temporary directory because vendor recipes do not support spaces reliably. The final image is copied to `.build/output/km0_km4_image2.bin`. Upload uses 921600 baud; debug serial uses **9600 baud, 8N1**. Upload success requires the vendor's `All images are sent successfully!` message, not merely exit code zero. The script isolates vendor tools for both compilation and upload because the vendor uploader uses images in its tool directory rather than the supplied input-file path.

## Acquisition and Recovery

The IMU runs at 52 Hz with +/-4g and +/-500 degrees/s ranges. Acceleration is raw * 0.000122 g; gyro is raw * 0.0175 degrees/s. BDU protects individual data words, auto-increment reads the 12-byte gyro/acceleration block, and data-ready must indicate both sensors. FIFO and interrupts are disabled. High-performance mode is selected during bring-up; board current has not been measured.

Initialization checks reset completion and verifies each setting by read-back. It waits 150ms after configuration. Hardware I2C runs at 100kHz with 20ms short / 50ms whole-burst deadlines. Every FIFO and completion wait is bounded. RESTART is enabled and attached to the register-pointer byte, as required by this USI controller. The software fallback uses open-drain GPIO with a 25us half-cycle. Three transfer failures or one second without fresh data takes the IMU offline; the application retries every two seconds. A bounded address scan primes the hardware controller during initialization. A standalone hardware diagnostic runs after discovery failures on the first attempt and every fifteenth retry. Diagnostic counters report successful acquisitions, read errors, and initialization attempts. Counter `starts=1` includes initial boot.

No MPU6050 DMP firmware is loaded. This driver produces raw six-axis data, with no quaternion or drift correction. Legacy DMP helper files remain in this copy for reference and are not used by the active driver. Orientation protocol fields are reserved and never notified.

## Serial and LED

Fresh raw IMU snapshots and status/counters are logged at approximately 1 Hz, independently of faster acquisition. Printing every 52 Hz sample as readable text would exceed 9600-baud capacity. Logs run through a bounded queue; overloaded debug output may be dropped.

```text
IMU,t_ms=15000,ax=0,ay=0,az=8197,gx=0,gy=0,gz=0
STATUS,t_ms=15000,version=4,flags=0x81,tx_dropped=0,sample_gaps=0
IMU_STATS,samples=700,read_errors=0,starts=1
IMU_STATE,mode=raw,state=4,fifo_resets=0
```

These lines illustrate the format, not measured results. States: 0 offline, 1 reset wait, 2 configure, 3 settling, 4 raw running. With analog and GPS disabled, healthy status flags are 0x81. With fresh, in-range TMP36 data and GPS/pulse disabled, healthy flags are 0x87. Missing disabled sensors do not trigger LED faults.

The onboard blue LED (PA14) is steady during boot, pulses once every two seconds when ready, twice when BLE-connected, three times for an IMU fault, and blinks rapidly for a subsystem failure. Red/green remain unused. LED timing stops if the main loop stalls.

## BLE Transport

The active transport is Nordic UART Service (NUS), matching the supplied NU40 connection example. MPU6050 advertises `NU87-1`; NU-IMU advertises `NU87-2`. Sensor JSON lines are fragmented into at most 20-byte notifications, with CCCD gating, bounded queues and disconnect cleanup. Read the [wire contract and receiver integration notes](docs/nus-telemetry.md) before changing the receiver. The former custom binary service is no longer advertised.

```sh
python tools/ble_receiver.py --name NU87-2 --jsonl readings.jsonl
```

## Validation and References

Native sanitizer tests cover LSM6DS3 identity/address/pin discovery, register verification, signed little-endian decoding, fresh-data gating, stale-data recovery, transaction failures and timer wraparound. Hardware transaction tests additionally cover command sequencing, byte transport, address rejection, missing receive data, missing STOP, FIFO stalls and timer wraparound. Existing ADC, parser, bus and LED tests remain. Python tests cover packet scales, UUIDs and receiver errors/reconnection. Hardware evidence is recorded in [the validation record](docs/power-validation.md).

- [NUCODE board guide and schematic](https://nucode.gitbook.io/nuseries/downloads/nu-87-tinydk)
- [ST LSM6DS3 register driver](https://github.com/STMicroelectronics/lsm6ds3-pid)
- [ST LSM6DS3TR-C register driver and conversion constants](https://github.com/STMicroelectronics/lsm6ds3tr-c-pid)
- [Official Realtek Arduino core](https://github.com/Ameba-AIoT/ameba-arduino-d)
