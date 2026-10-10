# MPU6050 soldier compatibility capture

2026-10-11: built and flashed `firmware/nucode-project` from the `firmware` branch, with default SOLDIER_ID=1, MPU6050 raw mode, TMP36 and pulse enabled, UART GPS disabled. The upload tool confirmed all images sent successfully.

Both soldier project directories now use manufacturer advertisements, replacing their legacy NUS JSON transport. Their packet format matches the NU40 gateway tested in HW_TEST_2026-10-11.md. The NU-IMU profile defaults to ID 2.

A PC Bluetooth scan received this real soldier packet:

```text
PKT:1201bca56000004117090a00000a000000e2,RSSI:-60
```

The unchanged `Tristan_Cha` decoder at f5f2771 accepted the length, version and CRC: ID 1, seq 42428, node time 96 s, check-in, HR 65, temperature 23.27 C, activity 10, local x=0 m/y=1 m. The first check-in carries collected baseline values; these are not necessarily the instantaneous values at transmission. No absolute position or clinical measurement accuracy is established by this test.

The same captured bytes were replayed through a pseudo-terminal into this branch's real serial reader, and retrieved through the actual REST and WebSocket handlers:

- `api-state-live-radio.json`: GET /api/state response.
- `ws-state-live-radio.json`: /ws state message.
- `gateway-lines-live-radio.txt`: captured packet formatted for the serial reader.

This run validates soldier radio output and tactical-c2 compatibility. It does not claim a new physical NU40-to-PC test; the gateway was on the commander's computer. The prior team gateway test is documented separately in HW_TEST_2026-10-11.md.

At 134 s the soldier log showed 9967 IMU samples, read_errors=0, starts=1 and sample_gaps=0. Both soldier builds and native/Python protocol parity tests passed. This backend passed 19 tests and its Angular production build passed.

## Commander setup

Use the already-flashed NU40 advertisement gateway, not the old NUS client. Start tactical-c2 with C2_SOURCE=serial, C2_SERIAL_PORT set to the gateway's current port and C2_SERIAL_BAUD=115200. Close other serial readers first. The latest soldier firmware sends current BPM, temperature, activity and position every second from startup; an alert replaces the routine packet for that second. It does not wait for baseline collection. The capture above is historical, before this cadence change. Serial mode now marks a silent soldier stale after 5 s (override with C2_STALE_AFTER_S for older firmware). The WebSocket state publisher runs once per second, so display latency also includes that phase and radio reception.

The changes in `codex/tristan-sensor-compat` map unavailable readings to null and prevent missing HR from corrupting PSI/baselines. The wire protocol is unchanged, so unmodified Tristan_Cha can already receive valid packets, but needs these fixes to display missing sensors correctly. Phone GPS remains on its existing Wi-Fi path.
