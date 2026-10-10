# Backend handoff

These synthetic API examples belong to the `codex/soldier-sensor-integration` branch, based on `soldier-rework`. They do not establish that this branch's NUS transport is compatible with the new backend.

Use the integrated transmitter, gateway, and backend together from [the integration branch](https://github.com/csihyeon05/D4D-x-EDTH-Hackathon---Team.-Squad-HUD/tree/codex/soldier-sensor-integration). Its [handoff documentation](https://github.com/csihyeon05/D4D-x-EDTH-Hackathon---Team.-Squad-HUD/tree/codex/soldier-sensor-integration/docs/frontend-handoff) is authoritative.

Phone GPS is posted over Wi-Fi to the backend, with an explicit soldier ID; it is not a UART GPS sensor attached to these boards. Missing readings are JSON null.
