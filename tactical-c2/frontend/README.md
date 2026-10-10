# Frontend

Angular 21 map display (COP) for the commander. It connects to the backend WebSocket `/ws`: same origin when the backend serves it, port 8000 when run with `ng serve`.

```bash
npm install
npx ng serve     # dev server with reload: http://localhost:4200
npx ng build     # output in dist/; the backend then serves it at http://localhost:8000
npx ng test      # unit tests (Vitest)
```

Requires Node 20.19+ or 22.12+. Start the backend first; see [../README.md](../README.md).

**Without a backend:** open http://localhost:4200/?mock. The app then runs from built-in mock data (`src/app/mock.ts`): five soldiers with phone GPS, one with no pulse reading, one without a phone, one silent node, and one who becomes critical after 25 s. A **MOCK DATA** chip shows in the header.

## Screen

- **Action required (top, red):** appears while any soldier is CRITICAL. Shows the latest commander briefing (from the local LLM when connected, otherwise the backend's rule template; labelled with its source) and one line per critical soldier: what is wrong, the action to take, and the evidence (HR vs baseline, movement, GPS, last packet). *Show on map* selects the soldier; *Acknowledge* calls the backend ack. The per-soldier lines come from alert data (`CRITICAL_ACTIONS` in `app.ts`), so they still work if the LLM is down.
- **Squad positions:** soldiers as dots from their bound phone GPS (`soldiers[].gps`), on a metre grid fitted to the squad. Satellite imagery (Esri World Imagery tiles) under the grid when the laptop has internet; offline the tiles just do not load and the grid remains. The *Satellite / Grid only* button in the map footer switches it (remembered per browser). Ring = GPS accuracy, tick = heading, dashed line = recent track, faded dot = fix older than 10 s. Soldiers without a phone are listed under the map. The node's local dead-reckoning x/y is a separate frame and is not drawn.
- **Vitals:** one row per soldier, most severe first: status, heart rate, skin temperature, estimated PSI, activity, time since last packet. `–` = no reading. Heart rate uses `hr_display_bpm`: while the pulse is briefly lost the last value stays for up to 8 s, greyed and marked `*` (detail card: *Reacquiring · last value N s ago*); after that it shows `–` / *Sensor signal unavailable*. A soldier silent for more than 10 s turns UNKNOWN.
- **Selecting a soldier:** click a dot, table row or alert. The soldier is highlighted on the map and in the table, and a detail card shows each vital against its baseline, the alert and its interpretation, GPS, dead reckoning and radio details. Click empty map to clear.
- **Below the map:** alert log and AI briefing. Demo scenario buttons appear only with the backend simulator.
