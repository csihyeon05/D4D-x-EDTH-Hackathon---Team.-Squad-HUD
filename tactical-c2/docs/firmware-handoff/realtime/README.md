# Live display telemetry validation

The soldier now sends current BPM, TMP36 temperature, activity and local PDR
position once per second from startup. An alert replaces the routine check-in
for that second. Baseline collection remains independent and never substitutes
averages for current measurements. Missing BPM remains zero on air and null in
the compatible backend. Raw pulse waveforms and raw IMU axes are outside this
requested display stream. Phone GPS retains its separate Wi-Fi path.

## Hardware and radio evidence

- Uploaded the MPU6050 / TMP36 / pulse profile (ID 1) successfully.
- The NU-IMU profile (ID 2) builds; it was not connected for flashing in this run.
- A 45-second PC BLE scan contained a UART-open reset. Excluding the single
  pre-reset packet, 39 unique valid packets covered node seconds 1 through 43.
- Reception interval median: 1.001 s; maximum: 2.882 s. The PC scanner did not
  report seconds 6, 7, 12 and 14. Advertising is unacknowledged; this is not a
  guarantee of lossless delivery or a physical NU40 end-to-end test.
- Valid BPM fields included 53, 57 and 58; other packets explicitly reported
  unavailable BPM. This validates transport, not pulse measurement accuracy.
- Serial remains 9600 baud. Vendor BLE start/stop prints can interleave with
  queued application lines; use decoded BLE records for this timing evidence.

## Software validation

Both sensor profiles passed native tests and the three Python protocol/rule/PDR
parity tests against tactical-c2. Both Realtek target builds passed. The decoder
and actual backend serial reader accepted all 39 captured packets replayed over
a pseudo-terminal. REST and WebSocket exposed the latest values; the periodic
WebSocket update was also checked. The backend passed 20 tests.

Serial-mode stale timeout is now 5 seconds in codex/tristan-sensor-compat,
overridable with C2_STALE_AFTER_S. Its existing WebSocket publishes once per
second, adding up to one publication interval after receipt. No wire-format
change or NU40 reflash is needed for the higher transmission cadence.

The local node averages its alert baseline; the existing commander uses the
first valid measurements as its baseline. They are not guaranteed identical.
Historical records describing 120 +/- 30 second check-ins predate this change.
