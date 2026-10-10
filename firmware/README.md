# Firmware projects

All three projects below use the same 18-byte BLE advertisement protocol consumed by `Tristan_Cha/tactical-c2`. Soldier projects no longer transmit NUS JSON.

| Project | Board | Default soldier ID | Sensors |
|---|---|---|---|
| nucode-project | NU87 | 1 | MPU6050, TMP36, Pulse Sensor |
| nucode-project-for-nucode-imu | NU87 | 2 | NU-IMU, TMP36; pulse disabled unless explicitly enabled |
| nu40-commander | NU40 nRF52840 | Receiver | BLE scan to USB `PKT:` lines |

In a soldier project, run `sh tools/nu87.sh build` or `sh tools/nu87.sh upload /dev/ttyUSB0`. Profiles and IDs can be overridden with environment variables. Always match enabled analog sensors to actual wiring. Soldier debug UART is 9600 baud; commander USB output is 115200 baud.

The commander forwards `PKT:<36 hex characters>,RSSI:<dBm>` to the backend. Set `C2_SOURCE=serial`, `C2_SERIAL_PORT` to the commander port and `C2_SERIAL_BAUD=115200`. Current BPM, temperature, activity and position are sent every second from startup. Baseline collection does not block transmission; an alert replaces that second's routine check-in. This is a 1 Hz display update, not a raw pulse waveform stream. Phone GPS still enters the backend over Wi-Fi.

The companion `codex/tristan-sensor-compat` branch fixes missing-reading handling in `tactical-c2`: heart rate 0, temperature -32768 and position -32768 become null. It preserves the wire layout. Without that backend fix, reception works but unavailable readings can appear as real values and corrupt PSI or map positions.

Phone GPS goes directly over Wi-Fi to the existing backend GPS endpoint. It is not a sensor on either soldier board. Local PDR coordinates are not phone latitude/longitude.

Tests: `BACKEND_ROOT=/path/to/tactical-c2/backend sh tools/test.sh` in either soldier directory. Each suite includes native fault/recovery tests and Python/C++ protocol parity checks against the selected backend.
