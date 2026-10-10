# NU40 commander gateway

NUCODE NU40 DK / nRF52840 receiver for the `soldier-rework` 18-byte manufacturer advertisement protocol. This is a passive scanner, not a NUS connection client. It receives both MPU6050 and NU-IMU soldier profiles by soldier ID.

The parser, CRC, duplicate window and serial formatter come from `commander-firmware/gateway_core.h` on `codex/soldier-sensor-integration`. The board variant is the vendored official NUCODE variant; see its SOURCE.txt.

## Build and upload

```sh
~/.platformio/penv/bin/pio run -e nu40_commander
~/.platformio/penv/bin/pio run -e nu40_commander -t upload --upload-port /dev/ttyACM0
```

If needed, double-press RESET to enter the USB bootloader. No sensor wiring is required on the gateway. USB serial is 115200 baud. Only open one serial reader at a time.

Output: `PKT:<36 hexadecimal characters>,RSSI:<signed dBm>`. The integrated backend uses `C2_SOURCE=serial C2_SERIAL_PORT=/dev/ttyACM0 C2_SERIAL_BAUD=115200`. Other `GW,` lines are diagnostics and are ignored by the backend.

LED: short heartbeat every two seconds, a 75 ms flash on a forwarded packet, rapid blinking on initialization failure. The scanner resumes after every report, including unrelated or invalid advertisements.

A bounded 32-packet queue decouples scanning from USB. It can overflow while the USB host is absent or stalled; diagnostics report drops. Packets waiting five seconds are discarded, even without a USB host, so reconnection cannot publish an old backlog as current sensor data. CRC and version checks precede forwarding, and repeated soldier ID/sequence pairs are suppressed for 30 seconds. This protocol provides no encryption or authentication.

Phone GPS goes directly to the backend over Wi-Fi. It is not received from this board. Missing values and local PDR coordinates retain the backend contract.

## Validation

Run `sh test/run.sh` for native ASan/UBSan tests. Set `BACKEND_ROOT` to the integrated `commander-hq/backend` directory to also validate emitted lines with its Python parser.

The initial PlatformIO build passed (123624 bytes flash, 27900 bytes RAM). Hardware upload and live reception must be confirmed separately; a successful build alone is not a reception test.
