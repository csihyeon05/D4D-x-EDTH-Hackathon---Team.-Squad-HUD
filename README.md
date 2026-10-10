# Squad HUD

[![Python](https://img.shields.io/badge/Python-C2%20Backend-3776AB?logo=python&logoColor=white)](https://www.python.org/)
[![Angular](https://img.shields.io/badge/Angular-COP-DD0031?logo=angular&logoColor=white)](https://angular.dev/)
[![NU40](https://img.shields.io/badge/NU40-Wireless%20Gateway-03234B)](./firmware/nu40-commander)
[![Pytest](https://img.shields.io/badge/Pytest-24%20Passed-0A9EDC?logo=pytest&logoColor=white)](./tactical-c2/backend)

Squad HUD is a real-time physiological monitoring and situational awareness system developed by Team Squad HUD for the D4D × EDTH Hackathon 2026.

The system consists of **wearable sensor nodes** that collect physiological and motion data, a **NU40 wireless gateway** that receives sensor telemetry, and a **command dashboard** that displays the condition and location of connected personnel. The backend processes incoming measurements, maintains individual personnel states, and provides a shared data interface for the dashboard.

The repository contains the embedded firmware, gateway implementation, backend, frontend, hardware design files, and integration tests.

- **Soldier firmware** — Embedded sensor nodes using MPU6050 or NU-IMU configurations. An integrated node implementation transmits compact 18-byte advertisement packets and updates its state approximately once per second.
- **NU40 gateway** — Receives wireless sensor telemetry and forwards it to the command computer over a serial connection.
- **C2 backend** — Python backend that decodes incoming telemetry, maintains personnel states, associates GPS coordinates with soldier IDs, and handles missing or stale measurements.
- **Common Operational Picture (COP)** — Angular-based dashboard showing physiological readings, personnel locations, and communication status.
- **Hardware** — PCB design files and embedded hardware configurations.
- **Integration tests** — Automated tests for packet compatibility, backend processing, sensor firmware, and frontend behavior.

## System architecture

```text
┌─────────────────────────────┐
│     Wearable Sensor Node    │
│                             │
│  Heart Rate / Motion Data   │
│  MPU6050 / NU-IMU           │
│  Embedded Firmware          │
└──────────────┬──────────────┘
               │
        Wireless Telemetry
        18-byte Advertisement
               │
               ▼
┌─────────────────────────────┐
│         NU40 Gateway        │
│                             │
│  Wireless Packet Reception  │
│  Serial Forwarding          │
└──────────────┬──────────────┘
               │
          Serial / USB
               │
               ▼
┌─────────────────────────────┐
│          C2 Backend         │
│                             │
│  Telemetry Decoding         │
│  Personnel State Tracking   │
│  GPS Association            │
│  Stale Data Detection       │
└──────────────┬──────────────┘
               │
             API
               │
               ▼
┌─────────────────────────────┐
│       Squad HUD COP         │
│                             │
│  Physiological Monitoring   │
│  Personnel Location         │
│  Connection Status          │
└─────────────────────────────┘

         Phone GPS Input
                │
                ▼
           C2 Backend
```

## Telemetry and state management

Each wearable node is associated with a personnel identifier. Sensor packets received by the NU40 gateway are forwarded to the backend, where they are decoded and used to update the corresponding personnel state.

The integrated sensor node uses an 18-byte advertisement format. The packet specification and compatibility requirements are documented in [`docs/DATA_SPEC.md`](./docs/DATA_SPEC.md).

The backend also manages unavailable measurements and communication interruptions.

1. **Telemetry reception** — The gateway receives a sensor advertisement and forwards it through the serial connection.
2. **Packet decoding** — The backend parses the packet and associates the measurements with a soldier ID.
3. **State update** — The latest available sensor information is stored in the personnel state.
4. **GPS association** — Coordinates submitted through `POST /api/gps` are associated with the specified `soldier_id`.
5. **Heart-rate dropout** — The COP uses `hr_display_bpm` and `hr_status` to distinguish available and unavailable heart-rate measurements.
6. **Communication timeout** — In serial mode, a soldier is marked `UNKNOWN` when no packet has been received for more than 10 seconds. The threshold is configurable through `C2_STALE_AFTER_S`.
7. **Dashboard update** — The frontend reads the current personnel state through `/api/state` and updates the COP.

## Tech stack

- **Sensor firmware:** Embedded C/C++, MPU6050 and NU-IMU configurations
- **Wireless gateway:** NU40 development platform, serial communication
- **Backend:** Python, pytest, REST API
- **Frontend:** Angular, TypeScript
- **Hardware design:** PCB design files
- **Testing:** pytest, firmware test scripts, protocol parity tests, simulated telemetry

## Getting started

### Backend

The backend is located in `tactical-c2/backend`.

To run its automated tests:

```bash
cd tactical-c2/backend
python -m pytest
```

For the hardware integration setup, the backend supports serial telemetry input through environment variables.

```bash
C2_SOURCE=serial
C2_SERIAL_PORT=COM10
```

`COM10` was the port used during the October 11 hardware integration test. Replace it with the actual serial port when using another computer.

### Frontend

The Angular frontend is located in `tactical-c2/frontend`.

Install dependencies and build the frontend:

```bash
cd tactical-c2/frontend
npm install
npx ng build
```

The frontend was verified against simulated telemetry, mock mode (`?mock`), and a virtual serial port.

### Soldier firmware

The integrated sensor node is located in `soldier-firmware`.

Run its automated test suite:

```bash
cd soldier-firmware
sh tools/test.sh
```

The test suite includes protocol parity checks against the backend's Python reference implementation.

### NU40 gateway

The gateway implementation is located in `firmware/nu40-commander`.

Run the gateway compatibility tests from the repository root:

```bash
BACKEND_ROOT=tactical-c2/backend \
sh firmware/nu40-commander/test/run.sh
```

The tests check whether the backend parser accepts the gateway's telemetry vectors.

## Project structure

```text
docs/
  DATA_SPEC.md             Sensor packet specification
  ...                      Validation documentation

firmware/
  nucode-project/          MPU6050 sensor firmware
  nucode-project-for-nucode-imu/
                           NU-IMU sensor firmware
  nu40-commander/          NU40 gateway firmware
    test/                  Gateway compatibility tests

soldier-firmware/
  test/                    Sensor node and parity tests
  tools/                   Test scripts

tactical-c2/
  backend/                 Telemetry processing and state API
  frontend/                Angular COP dashboard
  docs/                    Integration and hardware test records

pcb/                       Hardware design files

INTEGRATION.md             Integration history and verification
```

## Integration and testing

The repository combines several independently developed components into a single integrated system.

The integration branch was verified on October 11, 2026.

| Component | Result |
|---|---|
| C2 backend | 24 pytest tests passed |
| Angular COP | Production build successful |
| Sensor node firmware | Automated tests passed |
| Protocol parity | 3 tests passed |
| NU40 gateway | Backend parser compatibility passed |
| Frontend simulation | Mock and virtual serial modes verified |
| Hardware communication | Live telemetry received |

### Hardware verification

A NU40 gateway was connected to the command computer through `COM10`, with the backend configured for serial input.

During the test, the backend received telemetry from sensor node `S1` at approximately one packet per second, including live heart-rate data.

The frontend was also tested against a simulated communication interruption, transitioning through live, reacquiring, and `UNKNOWN` states after the configured timeout.

Additional details are available in [`tactical-c2/docs/HW_TEST_2026-10-11.md`](./tactical-c2/docs/HW_TEST_2026-10-11.md).

## Status

The backend, frontend, sensor firmware tests, and gateway protocol compatibility checks have passed their documented integration tests.

A physical NU40 gateway has successfully delivered live sensor telemetry to the backend. The COP has been tested using simulated data and virtual serial communication.

These results verify the current software integration and initial hardware communication path. They do **not** establish complete multi-node field reliability, medical-grade physiological accuracy, or readiness for operational deployment.

## Documentation

- [`INTEGRATION.md`](./INTEGRATION.md) — Integration history and verification results
- [`docs/DATA_SPEC.md`](./docs/DATA_SPEC.md) — Sensor advertisement packet specification
- [`tactical-c2/docs/HW_TEST_2026-10-11.md`](./tactical-c2/docs/HW_TEST_2026-10-11.md) — Hardware integration test

---

Developed by **Team Squad HUD** for the **D4D × EDTH Hackathon 2026**.
