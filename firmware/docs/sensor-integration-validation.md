# Sensor integration validation

Date: 2026-10-11. Base: soldier-rework c7f3fb3. Firmware integration: 772b95d.

## Hardware exercised

NU-87 Tiny DK, NU-IMU at 0x6A with WHO_AM_I 0x6A, TMP36 on PB1/A4. IMU SDA PB21, SCL PB20, shared GND. Build: SOLDIER_ID=2 IMU_MODEL=lsm6ds3 CFG_PULSE=0 CFG_GPS=0.

Automatic upload initially failed. After an RTS reset restored serial boot output, upload completed with `All images are sent successfully!`. No flash erase was used.

At 179 seconds: 9,316 IMU samples, read_errors=0, starts=1, sample_gaps=0. The capture had no observed reboot. Serial output is 9600 baud; vendor BLE messages can interleave with application lines.

The first check-in at 120 seconds was received over PC Bluetooth:

```text
1202e6cb780000003b08080000000000005a
```

Backend CRC decoding: soldier 2, sequence 52198, node time 120 s, temperature 21.07 °C, activity 8, heart rate unavailable. One unique packet was observed. Repeated BlueZ callbacks may contain cached manufacturer data; they cannot establish on-air repetition count or radio power use.

The captured packet passed the real backend serial reader via a pseudo-terminal and appeared in both REST and WebSocket responses. See [live frontend payloads](../backend-handoff/README.md). This is a replay of actual radio data, not a physical commander gateway test.

## Automated checks

- Realtek AmebaD 3.1.9 builds: MPU6050 raw, MPU6050 DMP, NU-IMU, commander gateway.
- Native ASan/UBSan checks: sensor decoding, I2C failures and timing, DMP lifecycle/FIFO, LSM6DS3 recovery, hardware I2C transaction simulation, raw heading, node rules and packet encoding.
- Three Python/C++ parity tests passed.
- Gateway tests and its backend parser integration passed.
- Backend: 20 tests passed. Angular production build passed.
- Phone HTTPS certificate helper executed successfully; no private keys are committed.

## Remaining physical validation

- TMP36 parsed serial values ranged from 7.85 to 23.88 °C, with transient low readings; the last value was 21.18 °C. Acquisition and transport work, but noise, wiring stability and absolute accuracy are not validated. Do not treat these readings as calibrated body temperature.
- MPU6050 board upload/acquisition, physical commander reception, pulse sensor/finger signal, and real phone Wi-Fi GPS are not verified in this run.
- Raw gyro heading and DMP yaw drift remain unmeasured. Phone GPS stays separate from local PDR; absolute position and heading are not established by this bench test.
- Low-power burst duration and current consumption require radio/physical measurement.
