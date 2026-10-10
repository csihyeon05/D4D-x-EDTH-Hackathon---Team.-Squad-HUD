# Data Specification v1

Contract between `soldier-firmware/`, `commander-firmware/` and `commander-hq/`. All nodes and the gateway are NUCODE NU-87 (RTL8720DF).

Change all three together, and bump `PROTOCOL_VERSION` when the packet layout changes. Reference implementation: `commander-hq/backend/app/protocol.py`.

## 1. When a node transmits

Nodes send current display telemetry once per second:

| Trigger | Packet type | Notes |
|---|---|---|
| Alert rule fires (section 3) | `ALERT` (0x1) | Same code not repeated within 60 s |
| Report button pressed | `ALERT` (0x1) | `SITREP_CONTACT` or `SITREP_SOS` |
| Check-in timer expires | `CHECKIN` (0x2) | Every 1 s, without jitter |
| Power-on | `CHECKIN` (0x2) | Live check-ins start without waiting for baseline collection |

A check-in contains current BPM, temperature, activity and local position. Alerts replace the routine packet for that second. Local baseline averaging runs independently; the commander currently takes its first valid check-in values as its baseline. Raw waveforms are not transmitted. BLE advertisements repeat continuously until the next live update; delivery is not acknowledged.

## 2. Packet layout (18 bytes, little-endian)

| Offset | Size | Field | Type | Unit / encoding |
|---|---|---|---|---|
| 0 | 1 | `head` | uint8 | high nibble = version (1), low nibble = packet type |
| 1 | 1 | `soldier_id` | uint8 | 1..255 |
| 2 | 2 | `seq` | uint16 | increments per packet, wraps |
| 4 | 2 | `t_s` | uint16 | seconds since node power-on |
| 6 | 1 | `code` | uint8 | alert code (section 3); 0 for check-in |
| 7 | 1 | `hr_bpm` | uint8 | beats per minute; 0 = no reading |
| 8 | 2 | `skin_temp` | int16 | °C × 100 (34.56 °C → 3456); −32768 = no reading |
| 10 | 1 | `activity` | uint8 | 0..255, mean \|(\|accel\| − 1 g)\| over last 2 s, 1 g = 255; 0 if the IMU is offline |
| 11 | 2 | `x` | int16 | east position, decimetres from the origin; −32768 = unavailable |
| 13 | 2 | `y` | int16 | north position, decimetres from the origin; −32768 = unavailable |
| 15 | 1 | `gas` | uint8 | 0..255 raw gas sensor level; 0 while no sensor is fitted |
| 16 | 1 | `hops` | uint8 | 0 at origin; each relaying node adds 1 |
| 17 | 1 | `crc8` | uint8 | CRC-8, poly 0x07, init 0x00, over bytes 0..16 |

A relaying node changes only `hops` (+1) and `crc8` (recomputed), and drops any packet whose (`soldier_id`, `seq`) it has already forwarded. Relaying is not implemented yet; nodes send `hops` = 0.

`seq` starts at a random value at power-on, so packets from a rebooted node are not dropped as duplicates.

## 3. Alert codes

| Code | Name | Condition | Commander severity |
|---|---|---|---|
| 0x00 | `NONE` | Check-in | OK |
| 0x01 | `HR_SPIKE_IMMOBILE` | HR ≥ baseline + 45 while immobile ≥ 10 s | CRITICAL |
| 0x02 | `HR_LOW_IMMOBILE` | HR ≤ 45 while immobile ≥ 10 s | CRITICAL |
| 0x03 | `FALL_IMMOBILE` | Impact ≥ 2.5 g, then immobile ≥ 10 s | CRITICAL |
| 0x04 | `HEAT_STRAIN` | Skin temp ≥ baseline + 1.5 °C and HR ≥ baseline + 35 for 30 s | WARN |
| 0x05 | `HAZARD_ENV` | Gas ≥ 120 | WARN |
| 0x10 | `SITREP_CONTACT` | Button: enemy or target sighted | WARN |
| 0x12 | `SITREP_SOS` | Button: help needed | CRITICAL |

"Immobile" means `activity` < 8. All thresholds are demo defaults; tune them with measured data (`commander-hq/backend/app/node_rules.py`).

## 4. Node processing

1. Sample heart rate, skin temperature, IMU (≥ 50 Hz, for PDR) and gas.
2. Position: GPS when it has a fix. Otherwise PDR (step detection on \|accel\|, Weinberg stride length, DMP yaw heading; reference: `commander-hq/backend/app/analysis/pdr.py`) continues from the last GPS position. The PDR heading offset to true north is learned from GPS tracks.
3. Origin: a shared area-of-operations point if the node is built with one; otherwise the node's power-on position. Only a shared origin puts all soldiers in one frame.
4. Every second, evaluate the rules in section 3. Reference: `node_rules.py`. Rules whose inputs have no reading are skipped.
5. When a rule fires or a timer expires, build the packet and update the continuously advertised data (section 5) at the lowest TX power that reaches the next hop. (TX power control is not implemented yet.)

## 5. Radio: BLE advertising

Nodes never connect. Each packet is a legacy, non-connectable advertisement (`ADV_NONCONN_IND`):

| AD structure | Bytes | Content |
|---|---|---|
| Flags | `02 01 06` | LE General Discoverable, BR/EDR not supported |
| Manufacturer data | `15 FF FF FF` + 18 bytes | Length 21, type 0xFF, company ID `0xFFFF` (little-endian), then the packet |

25 of 31 bytes used. `0xFFFF` is the Bluetooth SIG company ID for testing and internal use. Other devices may use it, so receivers must check length, version and CRC.

Advertisements are not acknowledged. A node repeats the latest packet (same `seq`) every 100–120 ms and updates the payload once per second without stopping advertising. Verify reception independently; controller update completion does not prove on-air delivery.

## 6. Gateway → commander (USB serial, 115200 baud)

The gateway (`commander-firmware/`) prints one line per new packet:

```
PKT:<36 hex characters>,RSSI:<dBm>
```

Example: `PKT:11030700d007018d800d02d204c8ff0c0147,RSSI:-63` = S3, seq 7, t = 2000 s, `HR_SPIKE_IMMOBILE`, HR 141, 34.56 °C, position (123.4, −5.6) m, 1 hop.

It forwards only the first copy of each (`soldier_id`, `seq`) within 30 s, so burst repeats and relayed copies reach the backend once. The backend ignores all other lines; the gateway prints `GW,...` counters every 10 s.

## 8. Phone GPS and unavailable values

The default build has no UART GPS module. The phone opens `/gps` over trusted HTTPS on the same Wi-Fi network and posts location fixes to `POST /api/gps`. Set its `soldier_id` explicitly to match the firmware. The backend exposes fixes in `gps.devices` and attaches a unique recent match to `soldiers[].gps`. Receipt freshness is at most 10 seconds; ambiguous device mappings are not attached. This path does not send GPS back to the node.

Phone latitude/longitude and node-local PDR x/y are different coordinate systems. They are not automatically fused or converted into a shared map origin. Raw gyro heading requires a stationary startup and drifts; DMP also lacks an absolute yaw reference.

The backend decodes missing heart rate (0), temperature (−32768), and x/y (−32768) as JSON `null`. Consumers must support these values. Coordinates otherwise saturate at ±32767 decimetres. Update the backend with the firmware: older decoders interpreted the position sentinel as a real coordinate. Byte layout and CRC are unchanged.

See [frontend handoff](../backend-handoff/README.md) for REST, WebSocket and phone request examples. Those fixtures are synthetic unless explicitly marked as live captures.
