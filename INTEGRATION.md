# Integration branch

Single branch carrying everything verified on 2026-10-11, as the base for further changes.

| Path | Source branch | Commit | Contents |
|---|---|---|---|
| `firmware/` | `firmware` | `7752c18` | Soldier firmware, 2 profiles (`nucode-project` MPU6050, `nucode-project-for-nucode-imu` NU-IMU) and the NU40 commander gateway (`nu40-commander`) |
| `soldier-firmware/` | `codex/soldier-sensor-integration` | `6ddb76c` | Integrated soldier node (18-byte advertisement, live state every second) |
| `docs/` | `codex/soldier-sensor-integration` | `6ddb76c` | `DATA_SPEC.md` (packet contract) and validation records referenced by `soldier-firmware/` |
| `tactical-c2/` | `codex/tristan-sensor-compat` | `cf0ddc1` | Commander backend + COP (builds on `Tristan_Cha`) |
| `tactical-c2/frontend/` | `cop-frontend` | `471f276` | COP design taken from `commander-hq/frontend`, wired to the tactical-c2 backend |

Changes made while integrating (on top of the sources above):

- Backend: phone GPS bound to soldiers (`soldier_id` on `POST /api/gps`, `soldiers[].gps` in `/api/state`), ported from the commander-hq backend the new COP design relies on. Phone page has a *Soldier ID* field.
- Backend: serial mode marks a soldier UNKNOWN after more than 10 s without packets (`C2_STALE_AFTER_S` overrides).
- COP: heart-rate dropout display (`hr_display_bpm`, `hr_status`) kept in the new design.
- `soldier-firmware/test/test_node_parity.py` finds the Python reference in `tactical-c2/backend` (falls back to `commander-hq/backend`).

## Verified

- `tactical-c2/backend`: pytest 24 passed.
- `tactical-c2/frontend`: `ng build` OK; rendered against the simulator, `?mock`, and a virtual serial port (live → reacquiring → UNKNOWN after > 10 s).
- `soldier-firmware`: `sh tools/test.sh` all passed, including 3 parity tests against `tactical-c2/backend`.
- `firmware/nu40-commander`: `BACKEND_ROOT=tactical-c2/backend sh test/run.sh` passed (backend parser accepts gateway vectors).

Hardware on the commander laptop (2026-10-11): NU40 gateway on COM10 (firmware from `firmware`), backend `C2_SOURCE=serial C2_SERIAL_PORT=COM10`, S1 received at ~1 packet/s with live heart rate. See `tactical-c2/docs/HW_TEST_2026-10-11.md`.
