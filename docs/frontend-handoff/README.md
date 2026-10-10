# Frontend Backend Contract

The `*-sim.json` files were captured through the actual FastAPI REST and WebSocket handlers on 2026-10-11, with the built-in simulator and LLM disabled. They are **not live soldier measurements**. The base is `soldier-rework` commit c7f3fb3b382d2877cc57bc8f8a7b5a68c361404f.

- `api-state-sim.json`: response from `GET /api/state`.
- `ws-state-sim.json`: message from WebSocket `/ws`, wrapped as `{ "type": "state", "state": ... }`.
- `openapi.json`: generated HTTP API schema. WebSocket messages are not included in OpenAPI.
- The TypeScript interfaces are in `commander-hq/frontend/src/app/models.ts`.

Render `hr_bpm`, `skin_temp_c`, and `psi_est` as unknown when null. Missing sensor sentinels are now decoded as null, and cannot establish or corrupt a physiological baseline. A stale track must not be displayed as a current reading. Position is local metres (`x_m` east, `y_m` north), not latitude/longitude.

Start the backend from `commander-hq/backend`:

```sh
python -m venv .venv
.venv/bin/pip install -r requirements.txt
C2_SOURCE=sim C2_LLM_ENABLED=0 .venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8000
```

Use `http://127.0.0.1:8000/api/state` and `ws://127.0.0.1:8000/ws`. The socket also sends `{ "type": "briefing", "briefing": ... }`. `POST /api/scenario` selects a simulator scenario; query `GET /api/scenarios` for available names.

Transport distinction: this branch's firmware uses 18-byte manufacturer advertisements and gateway `PKT:<hex>,RSSI:<value>` lines. It does not use the earlier NU87 NUS counter demo. Do not feed NUS JSON directly into this backend's serial parser.

## Phone GPS over Wi-Fi

Ported from `Tristan_Cha` (`tactical-c2/backend/app/gps.py` and its browser pages). Phone GPS reaches the backend directly over Wi-Fi; it does not travel through the soldier board. Open `/gps` over HTTPS on the phone and enter the matching board `SOLDIER_ID`. The existing phone/device identifier remains separate.

- Phone request: `POST /api/gps`, example `phone-gps-request-example.json`.
- GPS snapshot: `GET /api/gps`, example `api-gps-example.json`.
- WebSocket event: `{ "type": "gps", "fix": ... }`.
- Every state includes `gps.devices`; each soldier includes `gps` when exactly one fresh phone is explicitly bound to that ID. Fixes older than 10 seconds or conflicting bindings are not attached to a soldier. Unbound phones remain visible in the GPS monitor.
- The examples use a synthetic Seoul coordinate and simulated sensor values. They are API contract examples, not a claim of a live phone or complete radio chain.
- WGS84 latitude/longitude are separate from IMU-based local `x_m`/`y_m`; they are not silently mixed. Unavailable local position is null and is omitted from trails and spatial grouping.

Windows: run `commander-hq/RUN_GPS.bat`. Linux/macOS: create a trusted TLS certificate for the laptop address, then run Uvicorn with `--host 0.0.0.0 --port 8443 --ssl-certfile cert.pem --ssl-keyfile key.pem`. Both devices must share Wi-Fi/hotspot access. The browser must trust the certificate and grant location permission. The imported `backend/tools/make_cert.py` can generate a development certificate; trusting it on the phone is a separate step.

The backend and GPS API suite now passes 20 tests, and the Angular production build passes. Physical phone reception has not yet been measured.

## Live NU-IMU radio capture, 2026-10-11

- `gateway-lines-live-radio.txt`: actual 18-byte BLE manufacturer data received from soldier 2, formatted as gateway lines.
- `api-state-live-radio.json`: actual REST response after feeding that capture through a pseudo-terminal into the backend serial reader.
- `ws-state-live-radio.json`: corresponding actual WebSocket response.

This validates NU-IMU/TMP36 transmitter → PC Bluetooth capture → replay through backend serial reader → REST/WebSocket. The PC substituted for a physical commander board; reception by that board remains unverified. The payload contains measured temperature 21.07 °C, activity 8, and null HR/GPS. Local position 0/0 is the stationary startup reference, not a measured geographic location.

Only one unique check-in was observed during this run. BlueZ delivered repeated callbacks containing the same payload after the advertised burst; callback count is not an on-air packet count or proof of burst duration. Temperature outliers remain under investigation; see the validation record.
