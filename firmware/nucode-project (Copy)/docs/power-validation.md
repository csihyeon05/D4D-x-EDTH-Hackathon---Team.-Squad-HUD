# NU-87 Power and Stability Validation Record

## Current Target

NUCODE NU-87 Tiny DK / RTL8720DF, official Realtek AmebaD 3.1.9 core, AMB26 target, MPU6050 raw acquisition with up to 50Hz BLE snapshots; optional MotionApps DMP 6.12 remains available. TMP36 and Pulse Sensor use calibrated 3.3V ADC scaling. GPS is disabled because no module is fitted; optional Serial1 NMEA remains configured at 9600 baud. The previous NU40 upload records and LSM6DS power settings do not validate this board.

## Confirmed Evidence

- NUCODE's start guide specifies AMB26 and supports Auto Upload Mode. The schematic confirms active-high RGB LEDs on PA13/PA12/PA14 and the sensor header pin assignments documented in README.md.
- An AMB26 build completed with the official core. The initial DMP build reported 865020 bytes (41% of its 2097152-byte program limit); later diagnostic builds differ.
- Flashing the DMP firmware to the connected CP2102N port `/dev/ttyUSB0` completed with `All images are sent successfully!`.
- Before the DMP update, an 8-second read of `/dev/ttyUSB0` at 9600 baud captured 1720 bytes of live telemetry. Status reported protocol v2, flags 0x22, and `sample_gaps=0`. MPU6050 was not detected; TMP36 was invalid, pulse raw/BPM were zero, and GPS had no fix. This verifies execution and logging, not attached-sensor operation or visual LED behavior.
- Native C++ ASan/UBSan tests passed, including MPU6050 byte order/sign, I2C transaction failures, ADC conversion, pulse timing, NMEA parsing, and periodic scheduling. Eight Python protocol/receiver tests passed.
- The ADC task has a dedicated 500Hz schedule on the core's 1000Hz RTOS tick. The main loop yields and slow 9600-baud logs run in a bounded, lower-priority queue. Debug records are limited to one per type per second.
- BLE peripheral initialization waits inside the vendor core until stack readiness; there is no application timeout around that call. The onboard boot indication remains steady during initialization.
- The Realtek characteristic notify function does not return transmission status. The status counter only records local failures, not complete radio packet-loss statistics.

## DMP Update Validation

- After uploading the DMP build, a 12-second capture at 9600 baud confirmed live protocol-v3 telemetry, `sample_gaps=0`, and `DMP,state=0,fifo_resets=0`. Repeated `MPU6050 not found` messages mean sensor communication and physical DMP operation remain unverified. TMP36 remained invalid, pulse raw/BPM were zero, and GPS had no fix.

- MotionApps 6.12 is pinned and vendored with its upstream license. Every uploaded sensor-memory chunk is verified by read-back before startup.
- Host tests exercise DMP initialization, startup motion gating, FIFO partial packets and backlog, mid-packet I2C failure, invalid quaternion recovery, stale data, reinitialization, and timer wraparound. These simulations do not prove the physical sensor accepts the firmware.
- Protocol v3 adds Q30 quaternion telemetry and updates raw ranges to +/-2g and +/-2000 degrees/s. The PC decoder validates quaternion norms and derives ZYX Euler angles.
- DMP startup requires at least 10 seconds plus a stillness window. The core image's stationary calibration cannot remove the need for an external absolute yaw reference.
- Actual DMP startup, FIFO cadence, measured angle accuracy and drift require a connected MPU6050. Do not interpret unit-test success as a measured drift result.

## PA12/PA13 Wiring Change

- MPU6050 software I2C moved to PA12/PA13 (Arduino indices 3/2). Initialization probes both directions, verifies WHO_AM_I and retains the matching mapping. The status LED now controls only PA14 blue; red/green buffer inputs follow bus traffic.
- Native tests verify reversed-wire discovery and that every LED state leaves both I2C pins untouched. Address-only ACK/NACK probes are tested separately from register transfers.
- GPS is disabled, including its UART initialization and periodic telemetry. The protocol retains its optional GPS characteristic for compatibility.
- The first remapped build uploaded successfully. A 25-second runtime capture showed both directions failing MPU register reads, idle lines high and `DMP,state=0`. ADC sampling continued with `sample_gaps=0`. A second diagnostic build uploaded successfully with at most 50kHz clock and separate address probes. Runtime logs report `ack=0` for both 0x68/0x69 in both PA12/PA13 directions, idle levels 1/1 and DMP offline. Later lines in the 20-second capture became fragmented; no successful MPU detection was observed. Wiring, supply, pull-ups and physical bus waveforms remain unverified.

## PA25/PA26 Wiring Change

- That diagnostic firmware used PA25/PA26 (Arduino indices 7/6), verified against the installed AMB26 variant and staged build sources. Both SDA/SCL directions are probed. Log labels come from configuration rather than hard-coded old pin names.
- These MCU nets are shared with native USB D-/D+. Native USB must remain disconnected while this I2C wiring is in use; the upload/debug CP2102N interface is separate. The schematic does not expose PA25/PA26 on the standard P1/P2 headers.
- Native sanitizer tests and all eight Python tests passed. The build reported 856828 bytes (40%); upload to `/dev/ttyUSB0` completed successfully.
- Live 9600-baud logs confirm the PA25/PA26 firmware is running, but report no MPU6050 detection and DMP state 0. This confirms deployment, not functional sensor communication.

## PB20/PB21 Return and Timing Diagnostics

- Current wiring is SDA=PB20 (Arduino 19, P2 pin 7), SCL=PB21 (Arduino 18, P2 pin 8). Runtime discovery confirmed WHO_AM_I=0x68 in this direction, so it is now tried first; reverse-direction fallback remains available.
- The first PB20/PB21 builds detected the MPU6050 repeatedly, but register setup failed (including 0x6C writes/reads). Detection alone does not validate DMP operation.
- The software clock-rise wait now polls for up to 50us before yielding for a millisecond. This avoids spending a full RTOS tick on ordinary line rise time with the then-current 5ms transaction deadline. A regression test models an 8us SCL rise and checks a register write completes within 2ms. Stuck-clock timeout tests still pass.
- Native sanitizer tests and all eight Python tests pass. GPS remains disabled and USB serial remains 9600 baud.
- The timing-change build uploaded successfully. A 25-second 9600-baud capture repeatedly identified MPU6050 at 0x68 on SDA=PB20/SCL=PB21, but setup still failed in reset/configuration (and once at the first image block). The timing change did not resolve physical communication reliability. No DMP-ready or quaternion output was observed.

## Raw Mode Validation

- `CFG_IMU_DMP=0` now selects raw acceleration/gyro register acquisition. DMP code remains selectable. Raw mode does not send quaternion packets. BLE protocol v3 retains the same raw scale and adds status bit7 for raw mode; bit6 covers either mode's initialization.
- Host tests cover raw configuration/read-back, fresh samples, signed register decoding, no synthetic orientation, transfer failures and stale data. DMP tests are still compiled separately with `CFG_IMU_DMP=1`. All native sanitizer tests and eight Python tests pass.
- I2C deadlines now use RTOS milliseconds instead of the core's fixed-clock `micros()` calculation. Early raw builds used 5ms short-transfer budgets and 20ms burst budgets; the current stability fix uses 20ms and 50ms respectively. These bounds are timeouts, not a guarantee of successful transfer.
- Initial raw-mode captures reached configuration-ready but went offline during acquisition. Changing the deadline clock alone did not produce valid raw samples.
- With a 20ms burst budget, physical raw data was captured, for example ax=17244, ay=496, az=2502, gx=-24, gy=-41, gz=11. The 20-second capture also showed repeated failures and reinitialization, including a 20ms burst timeout; continuous reliable acquisition was not established in that earlier run.
- Main-loop publication/logging schedules also use RTOS milliseconds converted to microsecond units, avoiding the vendor `micros()` clock assumption. The raw capture previously contained two IMU log timestamps only 20ms apart despite a 1Hz logging schedule.

- The earlier raw-mode build uploaded successfully (860924 bytes, 41%). A further 10-second capture included `IMU,t_ms=10213,ax=17234,ay=530,az=2372,gx=-24,gy=-40,gz=8` and another fresh sample, but still returned offline and reinitialized. That earlier run demonstrated only intermittent raw acquisition. Accuracy and BLE delivery remain unvalidated.

## Power Claims

No minimum-power claim or measured current figure is available. Wi-Fi is not started by the application. ADC acquisition, gyro operation, UART GPS, BLE advertising, USB-UART hardware, regulators, and the board's fixed power LED all consume current. Do not reuse NU40 current estimates for RTL8720DF.

The MPU6050 gyro stays active for six-axis measurements; cycling accelerometer-only low-power mode would lose gyro data. The RGB status indication uses short pulses. Debugging can be compiled out with `CFG_DEBUG=0`, but closing the PC monitor does not stop UART transmission by itself.

## Hardware Checks Still Required

1. Confirm stationary acceleration is about 1g in magnitude and gyro rates are near zero; move each axis to check signs and orientation.
2. Compare TMP36 against a reference thermometer and verify Pulse Sensor waveforms/BPM during motion and poor contact. A plausible analog value does not prove sensor presence.
3. Confirm the GPS model, supply/logic voltage, baud rate, and outdoor fix. Exercise loss of fix and stale data handling.
4. Record BLE packet counts/timestamp gaps on an actual PC adapter, including disconnect/reconnect and distance tests. The development environment currently lacks a usable BlueZ D-Bus service.
5. Run extended acquisition with all sensors attached, inspect stack headroom and `sample_gaps`, and test I2C unplug/recovery with pull-ups present.
6. Measure average/peak supply current for at least 60 seconds under identical sensor, RF, GPS, and supply conditions. Measure both connected and advertising modes, with debug enabled and disabled. Avoid supply paths that bypass the current measurement.

Sources: [NUCODE schematic/start guide](https://nucode.gitbook.io/nuseries/downloads/nu-87-tinydk), [official Realtek Arduino core](https://github.com/Ameba-AIoT/ameba-arduino-d).
