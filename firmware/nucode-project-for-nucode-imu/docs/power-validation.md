# NU-IMU Validation Record

## Scope

This copy targets NU-87 Tiny DK. Earlier runs used only NU-IMU with analog sensors and GPS disabled. On 2026-10-10 the user also confirmed TMP36 wiring; current configuration enables TMP36 and keeps pulse/GPS disabled. Previous MPU6050 observations do not validate this replacement sensor.

## Software Evidence

- Native AddressSanitizer/UndefinedBehaviorSanitizer tests passed, including the new LSM6DS3 state-machine and byte-order tests.
- All eight Python protocol/receiver tests passed.
- The official Realtek AmebaD 3.1.9 / AMB26 build completed: 852732 bytes, 40% of the program limit.
- The initial upload could not open the port because a serial monitor held it. The vendor uploader returned exit code zero despite that failure. The script now checks its explicit success message.

## Hardware Evidence

- Two uploads completed with `All images are sent successfully!`; the monitor confirmed protocol v4 at 9600 baud.
- First capture repeatedly identified WHO_AM_I=0x6A at address 0x6A with SDA=PB21 / SCL=PB20. Configuration read-back passed, but no fresh measurement was captured. Read errors and one-second stale-data recovery were observed; identification alone is not successful acquisition.
- The subsequent diagnostic build selected that mapping first. Its capture showed no address ACK in either mapping and zero samples. Physical power-cycle verification is pending.
- A third upload lowered the software I2C clock ceiling to 20kHz, selected high-performance sensor mode, and added line/error diagnostics. Upload and all native/Python tests passed. A 20-second capture still showed address NACK (`err=2`) at both 0x6A/0x6B in both mappings, with SDA/SCL high (`lines=11`). No clock-stuck timeout was observed. Thus a bus held low is not the observed failure; sensor supply, connection, interface selection and electrical waveforms remain unverified.
- The user reported no wiring changes. No full sensor power cycle has been confirmed.
- No successful NU-IMU acquisition, BLE delivery, angle accuracy, drift, or supply-current claim is established yet.

## Remaining Checks

Measure acceleration magnitude and gyro bias with the sensor stationary, then verify axis signs under controlled movement. Check long-duration reset/disconnect behavior and BLE delivery with a PC receiver. Measure supply current before making power claims. Test other sensors only after wiring and enabling them.

## Independent Hardware I2C Diagnostic

- The installed SDK includes USI I2C support for SDA=PB21 / SCL=PB20, although the Arduino Wire wrapper rejects that mapping. A separate diagnostic now checks those pins at 100kHz after initial software discovery fails and every fifteenth retry. It restores GPIO pinmux afterward.
- The SDK timeout-write entry point could not be linked because its precompiled object references a missing `usi_i2c_reset`. The diagnostic instead uses peripheral commands with explicit 20ms polling deadlines. This is diagnostic code, not a validated replacement acquisition driver.
- Build and upload completed: 856828 bytes (40%), with the uploader's explicit success message.
- A live diagnostic returned `probe=-1, read=0, id=00, flags=03A00001` for both 0x6A and 0x6B. The hardware TX-abort flag was set. Software probing simultaneously reported address NACK in both directions, with both lines high. The abort-source register was not recorded, so its precise hardware abort cause is not claimed.
- Measurements remain unavailable. A physical pin-by-pin wiring description and full sensor power cycle have been requested. This result does not establish a defective sensor or a verified power fault.
- Waiting-for-data diagnostics no longer read measurement registers, avoiding accidental consumption of a partially ready sample.

## Confirmed Hardware Identity and Acquisition Migration

- The user explicitly confirmed the sensor is connected to PB20/PB21. No other GPIO pairs are being selected.
- A full hardware scan of 112 non-reserved addresses found exactly one responder, 0x6A. The initial combined-read diagnostic failed because it did not enable RESTART and attached its marker to the wrong command.
- With RESTART enabled and attached to the register-pointer byte, the live diagnostic reported `addr=6A probe=0 read=1 id=6A flags=03C00011 abort=00000000`. At 0x6B it reported `abort=00000001`, the SDK-defined 7-bit address NACK. The sensor identity is therefore confirmed on SDA=PB21/SCL=PB20 without a confirmed power cycle. Earlier claims that a power cycle was necessarily required were premature.
- The acquisition driver now uses that hardware transaction sequence at 100kHz, with bounded FIFO/receive/STOP waits and GPIO fallback. Its build and upload completed successfully (856828 bytes).
- All native sanitizer suites and eight Python tests passed. A new hardware-register simulation checks multi-byte reads, write framing, RESTART placement, address NACK, missing receive data, missing STOP, FIFO stalls, and clock wraparound. Simulation does not establish physical measurement accuracy.
- Before the acquisition firmware could be monitored, the host kernel reported CP2102N disconnection at 23:11:58 on 2026-10-10. The board was absent from the USB serial-port list. Raw acquisition and sustained stability on the new hardware path remain unverified pending USB reconnection.

## Common Ground Connected: First Successful Acquisition

- The user reported the missing sensor ground and then connected it. The next upload succeeded and the sensor retained CTRL1_XL/CTRL2_G/CTRL3_C = 38/34/44 after settling.
- The capture reached 8,322 samples at approximately 160 seconds, with `read_errors=0` and `starts=1`. It contains 160 raw six-axis snapshots at the slower serial reporting rate. This establishes actual measurement acquisition, beyond identification alone.
- The planned 180-second capture was interrupted at about 160 seconds by the temperature firmware upload starting too soon. The uploader and monitor overlapped, causing that upload to fail. The monitor was closed before retrying. This was an operator/tool sequencing error, not evidence of a spontaneous sensor reset.
- TMP36 was enabled on PB1/A4 after the user confirmed VCC=3.3V, VOUT=PB1, and common ground. Pulse sampling is separately disabled so an unconnected input cannot generate misleading BPM.
- Native firmware, LSM6DS3, hardware I2C simulation, and all eight Python tests passed. The temperature-enabled build uses 860924 bytes.
- Vendor tools are now isolated per build/upload; the vendor upload recipe otherwise selects its tool-directory image instead of honoring the input-file argument.

## IMU and TMP36 Concurrent Validation

The temperature-enabled upload succeeded. A 180-second serial capture completed: at 179 seconds there were 9,313 IMU samples, zero read errors, starts=1, and zero analog sampling gaps. TMP36 produced 178 valid reports: 22.20 C initially, 34.67 C peak after the user held the sensor, and 22.87 C finally. This validates acquisition and thermal response, not calibrated absolute accuracy. The startup -32768 sentinel precedes the first average. BLE delivery was not tested in this run. See [the full capture](validation/2026-10-10-imu-tmp36.log).
