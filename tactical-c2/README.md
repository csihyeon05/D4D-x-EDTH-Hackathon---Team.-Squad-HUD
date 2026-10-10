# Squad Tactical C2 — Offline Edge COP

Track 4 (Tactical Edge) prototype. Soldier-worn nodes stay radio-silent and
transmit 18-byte alerts only on anomalies; an offline commander laptop
correlates them, shows a Common Operational Picture, and phrases a briefing
with a local small language model.

```
 soldier nodes (ESP32)        gateway ESP32         commander laptop (offline)
 sensors → PDR → rules  ──ESP-NOW──►  USB serial  ──►  FastAPI backend ──WebSocket──► Angular 21 COP
          (silent unless                               │  parse · assess · squad correlation
           a rule fires)                               └► local sLLM (OpenAI-compatible) → briefing
```

## Quick start

Requirements: Python 3.11+, Node 20.19+ / 22.12+.

```bash
# 1) backend
cd backend
pip install -r requirements.txt
uvicorn app.main:app --port 8000          # simulator mode by default

# 2) frontend (dev, hot reload) — open http://localhost:4200
cd frontend
npm install
npx ng serve
```

Single-port offline mode: run `npx ng build` once in `frontend/`; the backend then
serves the COP itself at http://localhost:8000.

## Configuration (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `C2_SOURCE` | `sim` | `sim` = built-in squad simulator, `serial` = real gateway |
| `C2_SERIAL_PORT` | `/dev/ttyUSB0` | e.g. `COM3` on Windows |
| `C2_SERIAL_BAUD` | `115200` | |
| `C2_LLM_API` | `ollama` | `ollama` = native `/api/generate`, raw ChatML prompt, JSON mode; `openai` = any OpenAI-compatible `/chat/completions` |
| `C2_LLM_BASE_URL` | `http://127.0.0.1:11434` | (`openai` mode: base ending in `/v1`) |
| `C2_LLM_MODEL` | `operator` | Ollama model: kanana-2-3b-instruct weights with `TEMPLATE {{ .Prompt }}` |
| `C2_LLM_TIMEOUT_S` | `45` | |
| `C2_EXAMPLE_ORIGIN` | (empty) | `lat,lon` centre of the /example map; empty = newest phone fix, else a default field |
| `C2_LLM_ENABLED` | `1` | `0` = always use the rule template |
| `C2_SIM_SPEED` | `1.0` | simulator time factor (e.g. `5` for fast demos) |
| `C2_SIM_SQUAD_SIZE` | `8` | |

Without a reachable LLM the backend falls back to a deterministic rule-based
briefing, so the demo never depends on the model being up.

## Briefing safety

The engine decides what is true; the LLM only phrases it. The model receives
a compact facts JSON (`Engine.facts`). If its output mentions a callsign that is
not in those facts, or gives a named soldier a condition (heat, fall, critical, ...) that
their alerts do not show, the output is discarded and the template briefing is shown
instead (`briefing._grounded`). Every briefing on the COP shows its source.

## Example run (`/example`, EXAMPLE chip on the COP)

Opens in a new tab and runs separately from the live picture:

1. **Personas**: 8 subjects of the public PhysioNet *Wearable Device Dataset from Induced
   Stress and Structured Exercise Sessions* v1.0.1 (Hongn et al., 2025,
   doi:10.13026/he0v-tf17, ODC-By 1.0), Empatica E4 wristband. `backend/app/data/personas.json`
   is built by `tools/personas/build_personas.py` (needs internet once, ~40 min).
2. **Incident**: `ambush` (S1–S4 down, S3 impact), `casualty` (S6 fall) or `heat` (S2, S7).
   Measured HR/skin temperature/stillness drive the same node rules as the firmware; every sample
   is encoded to the 18-byte packet and decoded into a separate `Engine`.
3. **Operator brief**: `POST /api/example/brief` → `Briefer.recommend`: the local model writes a
   brief and picks actions from the fixed catalog (`app/recommend.py`: MEDEVAC, BUDDY_AID,
   REST_AND_COOL, WITHDRAW_FROM_SECTOR, REGROUP_AT_CP, HOLD_AND_OBSERVE).
   The validator drops unknown actions, unknown soldiers/sectors and targets that do not meet the
   action's precondition; rule recommendations fill gaps and are tagged `rule`. A brief that names
   an unknown soldier or gives a soldier a condition not in their alerts is replaced by the template.
4. **Solution**: the selected actions change what the soldiers do (evacuation, aid, rest, withdrawal).
5. **Outcome**: critical/warn counts, soldiers in the contact sector, casualty distance to the CP and
   HR recovery, before vs. after.

What is measured and what is modelled is listed on the page (`example.PROVENANCE`).

## Demo scenarios (simulator)

| Button | What happens | Expected result |
|---|---|---|
| Injury | one soldier: HR spike, stops moving | `HR_SPIKE_IMMOBILE` → CRITICAL |
| Unconscious | impact, then immobile, HR falls | `FALL_IMMOBILE` → CRITICAL |
| Heat strain | skin temp and HR rise while moving | `HEAT_STRAIN` → WARN (≈1–2 min) |
| Ambush | 3 nearby soldiers go prone, HR spike, one presses CONTACT | squad event **POSSIBLE CONTACT** |
| Gas hazard | gas cloud over the squad | squad event **HAZARD AREA** |
| SOS | button press | `SITREP_SOS` → CRITICAL |
| Node dark | one node stops transmitting | UNKNOWN after missed check-ins (≈4.5 min at speed 1) |

The header shows **radio silence %**: the share of sensor samples that never
went on air, compared with a design that streams every sample.

## Phone GPS node (stopgap for the missing GPS module)

The team has no GPS module, so a phone measures **latitude, longitude, accuracy
and heading** in its browser and posts one fix per second to the backend.

```powershell
RUN_GPS.bat        # HTTPS on :8443, creates a self-signed cert on first run
```

1. Put the phone and the laptop on the same Wi-Fi or phone hotspot.
2. On the phone open `https://<laptop-ip>:8443/gps` (the URL is printed at start).
   Accept the certificate warning once (self-signed). HTTPS is required:
   browsers block the Geolocation API on plain `http://` except `localhost`.
3. Enter a device ID (e.g. `P1`) and press **측정 시작**. Allow location (and
   motion/compass on iOS).

| Field | Source on the phone | Notes |
|---|---|---|
| `lat`, `lon` | `GeolocationPosition.coords` | WGS84 degrees |
| `accuracy_m` | `coords.accuracy` | radius in metres; ≤ 10 m is good |
| `heading_gps_deg` | `coords.heading` | course over ground, `null` when standing still |
| `heading_compass_deg` | `deviceorientationabsolute` (Android) / `webkitCompassHeading` (iOS) | where the top of the phone points, from magnetic sensor |
| `heading_deg` | chosen by backend | GNSS course if speed ≥ 1 m/s, else compass |

On the laptop, `https://localhost:8443/gps/pc` (opened automatically by
`RUN_GPS.bat`) shows the live readings with English labels and a **Download CSV**
button, plus links to every earlier log. CSV columns start with
`time_local, device_id, latitude_deg, longitude_deg, accuracy_m, heading_deg`.

Each phone identifies itself automatically as `<model>-<4 hex>` (e.g. `SM-S911N-3fa2`,
`iPhone-7c1e`); the code is random and kept on the phone, so several phones on the
same hotspot and the same link are tracked separately. The ID can be edited on the
phone; the monitor warns if two phones send the same ID.

Every fix is appended to `backend/data/gps_<start time>.csv`; the latest fix per
device is in `GET /api/gps`, in `/api/state` under `gps`, and pushed on the
WebSocket as `{"type": "gps", "fix": {...}}`. If the laptop is unreachable the
page buffers up to ~15 min of fixes and resends them; the **CSV** button saves
the phone's own copy. The page map uses OpenStreetMap tiles, which need
internet; without it the readings and upload still work, only the map is blank.

**Real suit (sensors + phone on the same outer):** double-click `RUN_SUIT.bat`. One HTTPS server on :8443 reads the NU40 gateway (auto-detected COM port) *and* receives the phone GPS, so the COP joins each soldier's vitals and position. It prints one QR per soldier (`-Soldiers 1,2` in the .bat); the link `https://<laptop-ip>:8443/gps?soldier=N` pre-fills and remembers the phone's Soldier ID. Laptop COP: `https://localhost:8443/`. The QR is also always available in the COP: header button **PHONE QR** (pick the soldier; served by `GET /api/phone-qr.svg?soldier=N`), so no console window is needed. Earlier C2 servers on :8000/:8443 are stopped first because only one program can own the gateway port.

**Binding a phone to a soldier:** enter the sensor board's `SOLDIER_ID` in the phone page's *Soldier ID* field. The COP draws that soldier at the phone position (`soldiers[].gps`) while the fix is under 10 s old and exactly one phone claims the ID; two phones with the same soldier ID are treated as ambiguous and not drawn.

## API

| Method | Path | |
|---|---|---|
| GET | `/api/state` | full COP snapshot |
| GET | `/api/health` | source and LLM status |
| POST | `/api/scenario` | `{"name": "ambush"}` (sim only; `reset` clears) |
| POST | `/api/soldiers/{id}/ack` | commander acknowledges an alert |
| POST | `/api/briefing` | generate a briefing now |
| GET | `/gps` | phone GPS measurement page |
| POST | `/api/gps` | one phone fix (JSON, see above) |
| GET | `/api/gps` | latest fix and track per phone |
| GET | `/gps/pc` | laptop monitor page with CSV download |
| GET | `/api/gps/csv` | download current CSV (`?name=gps_YYYYMMDD_HHMMSS.csv` for older logs) |
| GET | `/api/gps/logs` | list CSV logs |
| WS | `/ws` | pushes `{"type":"state"}` every second and `{"type":"briefing"}` |

## Layout

```
backend/app/
  protocol.py        18-byte packet encode/decode, CRC-8, gateway line parser
  node_rules.py      node-side trigger rules (firmware reference)
  analysis/pdr.py    pedestrian dead reckoning (firmware reference)
  analysis/assess.py sectors, estimated PSI, severity mapping
  analysis/squad.py  squad-level correlation (proximity clusters)
  engine.py          commander state, events, facts for the briefing
  briefing.py        local LLM client + grounding guard + template fallback
  simulator.py       synthetic squad using the real packet path
  serial_source.py   gateway reader
  main.py            FastAPI app, WebSocket, static COP
backend/tests/       unit + scenario tests (pytest)
frontend/src/app/    Angular 21 COP (signals, zoneless)
docs/DATA_SPEC.md    packet contract with the firmware
docs/HW_TEST_2026-10-11.md  NU40 gateway hardware test (gateway -> backend -> COP verified)
docs/proposal/       proposal (Korean, v3)
tools/hw/            gateway flashing, serial read, BLE scan, packet decode
firmware/            reference gateway sketch (ESP-NOW → serial)
```

## Hardware test (real gateway)

Verified 2026-10-11 with the NU40 gateway from the `firmware` branch: a soldier check-in reached the COP.
Details, issues and a checklist for the soldier boards: [docs/HW_TEST_2026-10-11.md](docs/HW_TEST_2026-10-11.md).

```powershell
powershell -ExecutionPolicy Bypass -File tools\hw\flash_nu40_gateway.ps1     # build + flash gateway
powershell -ExecutionPolicy Bypass -File tools\hw\read_serial.ps1 -Port COM10 # expect GW,... / PKT:...
powershell -ExecutionPolicy Bypass -File tools\hw\run_backend_serial.ps1 -Port COM10
```

Soldier boards must run `soldier-firmware` (advertisement protocol), not the NUS firmware in `firmware/nucode-project`.

## Known limitations

- PSI is **estimated** from skin temperature; the original index uses core temperature.
- All node thresholds are demo defaults; tune them with measured data.
- RSSI-based PDR correction (`PDR.correct`) is implemented but not yet wired into the engine.
