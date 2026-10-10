# NUS Sensor Telemetry v1

## Connection

| Item | Value |
|---|---|
| MPU6050 firmware | `NU87-1`, node ID 1 |
| NU-IMU firmware | `NU87-2`, node ID 2 |
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (central writes) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (central subscribes) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

The primary advertisement includes flags, the complete service UUID, and the complete six-character name (29 bytes total). Both RX and TX exist for BLEClientUart discovery. RX currently accepts writes but implements no commands. A connection alone does not enable transmission: the central must subscribe to TX notifications. The Realtek library resumes advertising after disconnection.

Sensor acquisition retains its existing timing. BLE sends latest IMU/vitals/orientation snapshots at up to 5 Hz, GPS/status at up to 1 Hz. It does not transmit every acquired IMU sample. Change `BLE_SENSOR_PERIOD_MS` only after measuring link capacity. USB debug remains 9600 baud.

## Framing

The TX byte stream is ASCII newline-delimited JSON. A notification carries at most 20 bytes. **One notification is not one JSON message.** Accumulate bytes per connection until LF, then parse the complete line. Do not mix bytes from the two soldiers. An empty line is sent on subscription to terminate a receiver's stale partial line. Disconnect/unsubscribe clears pending data. Maximum line length is 223 bytes including LF. The supplied PC decoder bounds its buffer and skips malformed/oversize lines.

A fixed queue retains the latest pending message of each type. An active line is completed before another starts. Round-robin selection prevents one sensor from starving others. Local queue replacements and write failures increment `dropped`; intentional rate limiting does not. `BLE_STATS.requests` counts notification requests, not acknowledgements. The vendor `notify()` API provides no delivery result. There is no application ACK/retransmission or claim of lossless delivery.

## Messages

Every line includes `v` (wire schema version 1), `id` (soldier ID), `n` (uint32 message sequence), `type`, and `ms` (uint32 node uptime in milliseconds). Sequence is shared across message types and resets on reboot; it is assigned when queued, so replacements may create gaps and round-robin scheduling can reorder sequence values across types. It is not a monotonically ordered radio delivery counter.

```json
{"v":1,"id":2,"n":12,"type":"imu","ms":1000,"model":"lsm6ds3","a":[2134,20,-7598],"g":[-38,28,-1]}
{"v":1,"id":2,"n":13,"type":"vitals","ms":1000,"tc":2220,"bpm":0,"pulse":0}
{"v":1,"id":2,"n":14,"type":"status","ms":1000,"sensor_v":4,"flags":135,"dropped":0,"gaps":0}
```

Examples illustrate the schema, not simultaneous measured readings.

| Type | Fields | Units / validity |
|---|---|---|
| imu | model, a[3], g[3] | Raw signed 16-bit XYZ; scale by model below |
| vitals | tc, bpm, pulse | tc: Celsius x100, null if unavailable; bpm: 0 unavailable; pulse: raw 12-bit ADC, 0 when disabled |
| gps | fix, lat_e7, lon_e7, alt_cm, speed_cms, sats | Position invalid when fix=0; disabled GPS sends nothing |
| quat | q30[4] | Signed Q30 w/x/y/z; emitted only when MPU DMP produces valid orientation |
| status | sensor_v, flags, dropped, gaps | Existing sensor schema 3 (MPU) / 4 (NU-IMU), status flags below |

MPU6050: acceleration raw / 16384 g, gyro raw / 16.4 degrees/s. LSM6DS3: acceleration raw x 0.000122 g, gyro raw x 0.0175 degrees/s. Do not use one model's scale for the other. Both current builds use raw mode; NU-IMU has no MPU DMP/quaternion output.

Status bits: 0 IMU online, 1 analog fresh, 2 temperature valid, 3 BPM available, 4 GPS fix, 5 GPS enabled, 6 IMU initializing, 7 raw mode. A plausible analog reading alone does not establish that a sensor is connected or calibrated.

## PC Receiver

Install `tools/requirements.txt` in a virtual environment, then run one process per board:

```sh
python tools/ble_receiver.py --name NU87-1 --jsonl soldier-1.jsonl
python tools/ble_receiver.py --name NU87-2 --jsonl soldier-2.jsonl
```

The receiver subscribes to NUS TX, reassembles lines, validates fields, converts units, records all valid messages and reconnects. It supports both IMU models irrespective of which project contains the script. `ble_receiver_legacy.py` and `protocol.py` retain the previous binary decoder solely for historical firmware; their old service is no longer advertised.

## NU40 and Existing Software Integration

The supplied NU87 counter example proves NUS connectivity, but does not specify the NU40 forwarding/parser implementation. This firmware replaces `{"n":counter}` with the messages above. A NU40 using BLEClientUart must forward each connection's full byte stream or accumulate complete lines before processing them. A parser that only accepts the counter JSON must be updated. Do not truncate at 20 bytes or prepend text between fragments.

Repository reference inspected: `origin/nu87-gateway`, commit `81770b2d6b4716707c3902e5c969a1e72bd645ef`, files `docs/DATA_SPEC.md`, `commander-firmware/gateway_core.h`, and `commander-hq/backend/app/protocol.py`. That implementation uses non-connectable advertisements carrying 18-byte CRC-protected alert/check-in packets; the backend consumes `PKT:<36 hex>,RSSI:<value>`. It neither connects to NUS nor parses raw IMU JSON. It is **not wire-compatible** with the supplied NUS example or these sensor streams.

The software team must use the NUS collector/decoder here, or add an explicit adapter in its gateway/backend. Raw sensors do not provide the old schema's activity/PDR position/alert fields directly. No fabricated position, gas reading, heart rate, or alert is emitted to pretend compatibility. End-to-end NU40/dashboard interoperability requires its actual receiver and cannot be established by a firmware build alone.

## Validation on 2026-10-11

Both real AmebaD/AMB26 builds passed (860924 bytes each), as did native sanitizer tests and 14 Python tests per project. Transport tests cover notification subscription gating, failed writes, disconnection cleanup, maximum chunk length, queue replacement/fairness, reassembly at every split, invalid input and model-specific scales.

The connected NU-IMU board was flashed as NU87-2. A PC Bleak client received 155 IMU, 106 vitals and 23 status messages in the first test cycle, with zero decoder errors and a maximum notification length of 20 bytes. Local drop and sensor read-error counters remained zero. The second PC scan did not find the board. Serial logs showed another connection and notification subscription after the first disconnect, followed by host USB disconnection at 00:01:52. The identity of that second central was not established; a second successful PC receive cycle is not claimed. MPU6050 hardware and NU40/dashboard end-to-end reception were not tested.
