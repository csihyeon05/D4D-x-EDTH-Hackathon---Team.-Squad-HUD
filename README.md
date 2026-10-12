# Squad HUD

[![Python](https://img.shields.io/badge/Python-C2%20Backend-3776AB?logo=python&logoColor=white)](https://www.python.org/)
[![Angular](https://img.shields.io/badge/Angular-COP-DD0031?logo=angular&logoColor=white)](https://angular.dev/)
[![TypeScript](https://img.shields.io/badge/TypeScript-Frontend-3178C6?logo=typescript&logoColor=white)](https://www.typescriptlang.org/)
[![NU40](https://img.shields.io/badge/NU40-Wireless%20Gateway-03234B)](./firmware/nu40-commander)
[![Pytest](https://img.shields.io/badge/Pytest-Backend%20Tests-0A9EDC?logo=pytest&logoColor=white)](https://pytest.org/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-D22128?logo=apache&logoColor=white)](./LICENSE)

Squad HUD is the open-source firmware, hardware and software for a real-time physiological monitoring and situational awareness system developed by **Team Squad HUD** for the **D4D × EDTH Hackathon 2026**.

The system uses **wearable sensor nodes** to collect physiological and motion data, a **NU40 wireless gateway** to receive sensor telemetry, and a **Common Operational Picture (COP)** to display the condition and location of connected personnel. A Python backend processes incoming packets, maintains individual personnel states, associates GPS coordinates with sensor IDs, and provides live information to the dashboard.

The repository holds the components of the system:

- **Soldier firmware** — Embedded firmware for wearable sensor nodes. Supports MPU6050 and NU-IMU configurations, collects sensor measurements and transmits compact telemetry advertisements. The integrated firmware uses an 18-byte packet format with an approximately 1 Hz update rate.
- **NU40 gateway** — Receives wireless advertisements from the sensor nodes and forwards telemetry to the command computer through a serial connection.
- **C2 backend** — Python service that decodes incoming packets, tracks personnel states, integrates GPS coordinates, handles missing measurements and detects communication timeouts.
- **Common Operational Picture (COP)** — An Angular web dashboard that displays physiological measurements, personnel locations, communication status and individual sensor availability.
- **Hardware** — PCB design files and embedded hardware configurations for the sensor and communication system.
- **Testing** — Automated backend tests, firmware tests, protocol compatibility checks and simulated telemetry for integration verification.

## System architecture

```text
┌───────────────────────────────┐
│      Wearable Sensor Node     │
│                               │
│  Physiological Sensors        │
│  MPU6050 / NU-IMU             │
│  Embedded Firmware            │
└───────────────┬───────────────┘
                │
        Wireless Telemetry
        18-byte Advertisement
                │
                ▼
┌───────────────────────────────┐
│          NU40 Gateway         │
│                               │
│  Wireless Packet Reception    │
│  Serial Telemetry Forwarding  │
└───────────────┬───────────────┘
                │
           Serial / USB
                │
                ▼
┌───────────────────────────────┐
│           C2 Backend          │
│                               │
│  Telemetry Decoder            │
│  Personnel State Management   │
│  GPS Association              │
│  Communication Timeout        │
└───────────────┬───────────────┘
                │
              API
                │
                ▼
┌───────────────────────────────┐
│          Squad HUD COP        │
│                               │
│  Physiological Monitoring     │
│  Personnel Location           │
│  Communication Status         │
└───────────────────────────────┘

             GPS Input
                │
                ▼
           C2 Backend
```

## Telemetry and monitoring

The monitoring system maintains a separate state for each connected sensor node. Every incoming telemetry packet is decoded by the backend and associated with the corresponding soldier ID.

The integrated sensor firmware transmits an **18-byte advertisement packet**. Its format and field definitions are documented in [`docs/DATA_SPEC.md`](./docs/DATA_SPEC.md).

The backend handles incoming measurements as follows:

1. **Sensor acquisition** — The wearable firmware collects physiological and motion measurements from the connected sensors.
2. **Wireless transmission** — The sensor node broadcasts a compact telemetry advertisement approximately once per second.
3. **Gateway reception** — The NU40 gateway receives the advertisement and forwards it to the backend through the serial interface.
4. **Packet decoding** — The backend parses the packet, extracts available measurements and associates them with the corresponding soldier ID.
5. **State update** — The latest measurements are stored in the personnel state and made available to the COP.
6. **GPS integration** — Coordinates submitted through `POST /api/gps` are associated with the specified `soldier_id`.
7. **Heart-rate availability** — The COP uses `hr_display_bpm` and `hr_status` to distinguish valid heart-rate measurements from unavailable data.
8. **Communication timeout** — In serial mode, a soldier is marked `UNKNOWN` when no telemetry has been received for more than 10 seconds. The threshold can be changed through `C2_STALE_AFTER_S`.

The dashboard obtains the latest personnel information through `/api/state`.

## Tech stack

- **Sensor firmware:** Embedded C/C++, MPU6050 and NU-IMU sensor configurations
- **Wireless communication:** NU40 development platform, wireless advertisements, serial telemetry
- **Backend:** Python, REST API, pytest
- **Frontend:** Angular, TypeScript
- **Hardware design:** PCB design files
- **Testing:** pytest, firmware test scripts, protocol parity tests and simulated telemetry

## Getting started

### C2 backend

The backend is located in `tactical-c2/backend`.

Run the automated backend tests:

```bash
cd tactical-c2/backend
python -m pytest
```

For the physical NU40 gateway, configure the backend to use serial telemetry input.

```bash
C2_SOURCE=serial
C2_SERIAL_PORT=COM10
```

`COM10` was used during the October 11, 2026 hardware integration test. Replace it with the serial port assigned to the gateway on your computer.

The communication timeout can also be configured:

```bash
C2_STALE_AFTER_S=10
```

### Common Operational Picture

The Angular frontend is located in `tactical-c2/frontend`.

Install dependencies and build the application:

```bash
cd tactical-c2/frontend
npm install
npx ng build
```

The frontend supports simulated telemetry and mock mode (`?mock`) for testing without the physical sensor network.

The dashboard reads personnel information from the backend and displays the latest available measurements, location data and communication state.

### Soldier firmware

The integrated wearable sensor firmware is located in `soldier-firmware`.

Run the firmware test suite:

```bash
cd soldier-firmware
sh tools/test.sh
```

The test suite includes protocol compatibility checks against the Python reference implementation used by the backend.

The firmware uses an 18-byte advertisement format and periodically updates its transmitted measurements.

### NU40 gateway

The gateway implementation is located in `firmware/nu40-commander`.

Run the protocol compatibility tests from the repository root:

```bash
BACKEND_ROOT=tactical-c2/backend \
sh firmware/nu40-commander/test/run.sh
```

These tests verify that telemetry vectors produced by the gateway implementation can be parsed by the backend.

For physical operation, connect the NU40 gateway to the command computer and configure the backend with the appropriate serial port.

## Project structure

```text
docs/
  DATA_SPEC.md                  Telemetry packet specification
  ...                           Protocol and validation documents

firmware/
  nucode-project/               MPU6050 sensor firmware
  nucode-project-for-nucode-imu/
                                NU-IMU sensor firmware
  nu40-commander/               NU40 gateway firmware
    test/                       Gateway compatibility tests

soldier-firmware/
  test/                         Sensor firmware and parity tests
  tools/                        Firmware testing scripts

tactical-c2/
  backend/                      Python telemetry processing and API
  frontend/                     Angular Common Operational Picture
  docs/                         Integration and hardware test records

pcb/                            Hardware design files

INTEGRATION.md                  Integration history and verification
LICENSE                         Apache License 2.0
```

## Integration and testing

The firmware, gateway, backend and frontend were developed as separate components and subsequently integrated into a single monitoring system.

The integration was verified on October 11, 2026.

| Component | Verification |
|---|---|
| C2 backend | 24 pytest tests passed |
| Angular COP | Production build successful |
| Soldier firmware | Automated tests passed |
| Protocol compatibility | 3 parity tests passed |
| NU40 gateway | Backend parser compatibility verified |
| Frontend simulation | Mock and virtual serial modes verified |
| Hardware communication | Live sensor telemetry received |

### Hardware verification

A physical NU40 gateway was connected to the command computer through `COM10`, with the backend configured to receive serial telemetry.

During the integration test, the backend successfully received live telemetry from sensor node `S1` at approximately one packet per second, including heart-rate measurements.

The frontend was also tested using simulated communication interruptions. The personnel state transitioned through live, reacquiring and `UNKNOWN` states according to the configured communication timeout.

The hardware test is documented in [`tactical-c2/docs/HW_TEST_2026-10-11.md`](./tactical-c2/docs/HW_TEST_2026-10-11.md).

## Status

The C2 backend, Angular frontend, sensor firmware tests and gateway protocol compatibility checks have passed their documented integration tests.

A physical NU40 gateway has successfully delivered live telemetry to the backend. The frontend has been verified using simulated data and virtual serial communication.

The system has **not yet been fully validated as an end-to-end multi-node deployment under field conditions**. Communication reliability, physiological measurement accuracy and behavior under extended operation require further hardware testing.

## Documentation

- [`INTEGRATION.md`](./INTEGRATION.md) — Integration history and verification results
- [`docs/DATA_SPEC.md`](./docs/DATA_SPEC.md) — Telemetry packet format and field definitions
- [`tactical-c2/docs/HW_TEST_2026-10-11.md`](./tactical-c2/docs/HW_TEST_2026-10-11.md) — Hardware integration test

## License

Licensed under the [Apache License 2.0](./LICENSE).
