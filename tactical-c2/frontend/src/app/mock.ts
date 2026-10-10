import { Alert, Briefing, C2State, PhoneGpsFix, Severity, Soldier } from './models';

/**
 * Front-end mock of the backend state (open the app with `?mock`).
 * Same shape as GET /api/state, with the cases real hardware produces:
 * missing pulse reading, no phone GPS, a silent node, and an alert.
 * Values are invented; see docs/frontend-handoff for captured payloads.
 */

const ORIGIN = { lat: 37.5665, lon: 126.978 };
const M_PER_DEG_LAT = 110_540;
const M_PER_DEG_LON = 111_320 * Math.cos((ORIGIN.lat * Math.PI) / 180);

interface MockSoldier {
  id: number;
  start: [number, number];     // metres east, north of ORIGIN
  velocity: [number, number];  // m/s
  hr0: number | null;          // null = no pulse contact
  temp0: number;
  phone: boolean;
  silentAfter?: number;        // node stops transmitting at this mission time
  injuredAt?: number;          // stops moving with HR spike
}

const SQUAD: MockSoldier[] = [
  { id: 1, start: [-20, 0], velocity: [0.6, 0.9], hr0: 72, temp0: 33.6, phone: true },
  { id: 2, start: [15, -10], velocity: [0.7, 0.8], hr0: null, temp0: 33.2, phone: true },
  { id: 3, start: [0, 25], velocity: [0.5, 1.0], hr0: 66, temp0: 33.9, phone: true, injuredAt: 25 },
  { id: 4, start: [-35, -25], velocity: [0.6, 0.7], hr0: 75, temp0: 33.4, phone: false },
  { id: 5, start: [40, 10], velocity: [0.4, 0.9], hr0: 70, temp0: 33.7, phone: true, silentAfter: 8 },
];

const STALE_AFTER_S = 270;
const SEV_COUNT_ORDER: Severity[] = ['CRITICAL', 'WARN', 'WATCH', 'UNKNOWN', 'OK'];

function psi(hr: number, temp: number, hr0: number, temp0: number): number {
  const t = (5 * (temp - temp0)) / Math.max(0.5, 39.5 - temp0);
  const h = (5 * (hr - hr0)) / Math.max(10, 180 - hr0);
  return Math.round(Math.max(0, Math.min(10, t + h)) * 10) / 10;
}

function band(value: number): string {
  return value >= 7 ? 'high' : value >= 4 ? 'moderate' : 'low';
}

export class MockSource {
  private readonly startMs = Date.now();
  private readonly acked = new Set<number>();
  private readonly alerts: Alert[] = [];
  private readonly briefings: Briefing[] = [];

  ack(id: number): void {
    this.acked.add(id);
  }

  brief(): Briefing {
    return this.briefFrom(this.state(), 'manual');
  }

  /** Template briefing, like the backend's fallback when no LLM answers. */
  private briefFrom(state: C2State, trigger: string): Briefing {
    const critical = state.soldiers.filter((s) => s.severity === 'CRITICAL').map((s) => s.callsign);
    const dark = state.soldiers.filter((s) => s.stale).map((s) => s.callsign);
    const parts = [`${state.soldiers.length} soldiers tracked.`];
    if (critical.length) parts.push(`${critical.join(', ')} critical: suspected injury, not moving.`);
    if (dark.length) parts.push(`No check-in from ${dark.join(', ')}.`);
    const b: Briefing = {
      text: parts.join(' '), source: 'template', model: null, latency_ms: 1,
      trigger, mission_time_s: state.mission_time_s, note: 'Mock data',
    };
    this.briefings.unshift(b);
    return b;
  }

  state(): C2State {
    const t = (Date.now() - this.startMs) / 1000;
    const alertsBefore = this.alerts.length;
    const soldiers = SQUAD.map((m) => this.soldier(m, t));
    const counts: Partial<Record<Severity, number>> = {};
    for (const sev of SEV_COUNT_ORDER) {
      const n = soldiers.filter((s) => s.severity === sev).length;
      if (n) counts[sev] = n;
    }
    const devices = soldiers.map((s) => s.gps).filter((g): g is PhoneGpsFix => g !== null);
    const packets = Math.floor(t / 30) * SQUAD.length + SQUAD.length + this.alerts.length;
    const state: C2State = {
      mission_time_s: t,
      soldiers,
      alerts: [...this.alerts],
      squad_events: [],
      counts,
      stats: { packets, bytes: packets * 18, rejected: 0, alerts: this.alerts.length, checkins: packets - this.alerts.length },
      grid: { size_m: 300, n: 3 },
      source: 'serial',
      emission: null,
      briefings: [...this.briefings],
      llm: { enabled: false, reachable: null, model: 'mock', base_url: '' },
      gps: { devices, csv: '' },
    };
    // The backend briefs automatically when an alert arrives.
    if (this.alerts.length > alertsBefore) {
      this.briefFrom(state, 'event');
      state.briefings = [...this.briefings];
    }
    return state;
  }

  private soldier(m: MockSoldier, t: number): Soldier {
    const silent = m.silentAfter !== undefined && t > m.silentAfter;
    const seenAt = silent ? m.silentAfter! - STALE_AFTER_S : t;  // pretend it went quiet long ago
    const moveT = m.injuredAt !== undefined ? Math.min(t, m.injuredAt) : t;
    const wobble = Math.sin(t / 7 + m.id) * 3;
    const east = m.start[0] + m.velocity[0] * moveT + wobble;
    const north = m.start[1] + m.velocity[1] * moveT;
    const injured = m.injuredAt !== undefined && t >= m.injuredAt;

    let hr: number | null = null;
    if (m.hr0 !== null) hr = Math.round(m.hr0 + (injured ? 58 + 4 * Math.sin(t) : 22 + 3 * Math.sin(t / 3 + m.id)));
    const temp = Math.round((m.temp0 + 0.3 + 0.1 * Math.sin(t / 11 + m.id)) * 100) / 100;
    const activity = injured ? 2 : Math.round(40 + 12 * Math.sin(t / 5 + m.id));
    const psiEst = hr !== null && m.hr0 !== null ? psi(hr, temp, m.hr0, m.temp0) : null;

    let severity: Severity = 'OK';
    let code = 0;
    if (injured) {
      code = 0x01;
      severity = 'CRITICAL';
      if (!this.alerts.some((a) => a.soldier_id === m.id)) {
        this.alerts.unshift({
          soldier_id: m.id, callsign: `S${m.id}`, code, label: 'HR spike, no movement',
          interpretation: 'suspected injury or panic', severity, sector: 'B2', t_s: m.injuredAt!,
          hr_bpm: hr, hops: 0, rssi: -71,
        });
      }
    } else if (psiEst !== null && psiEst >= 4) {
      severity = 'WATCH';
    }
    if (silent) severity = 'UNKNOWN';

    const lat = ORIGIN.lat + north / M_PER_DEG_LAT;
    const lon = ORIGIN.lon + east / M_PER_DEG_LON;
    const gpsAge = silent ? 45 : 1 + (m.id % 3);
    const gps: PhoneGpsFix | null = m.phone ? {
      device_id: `phone-${m.id}`, device_name: `Pixel S${m.id}`, soldier_id: m.id,
      lat, lon, accuracy_m: 3 + m.id * 1.5, alt_m: 38, speed_mps: injured || silent ? 0 : 1.1,
      fix_unix_ms: Date.now() - gpsAge * 1000, recv_unix_ms: Date.now() - gpsAge * 1000,
      heading_deg: (Math.atan2(m.velocity[0], m.velocity[1]) * 180) / Math.PI,
      heading_source: 'gps', id_conflict: false, age_s: gpsAge,
      track: Array.from({ length: 12 }, (_, i) => {
        const back = Math.max(0, moveT - (12 - i) * 4);
        return [
          ORIGIN.lat + (m.start[1] + m.velocity[1] * back) / M_PER_DEG_LAT,
          ORIGIN.lon + (m.start[0] + m.velocity[0] * back) / M_PER_DEG_LON,
        ] as [number, number];
      }).concat([[lat, lon]]),
    } : null;

    return {
      gps,
      soldier_id: m.id,
      callsign: `S${m.id}`,
      severity,
      stale: silent,
      acked: this.acked.has(m.id),
      active_code: code,
      active_label: code ? 'HR spike, no movement' : 'Check-in',
      interpretation: code ? 'suspected injury or panic' : 'routine check-in',
      hr_bpm: hr,
      hr_display_bpm: hr,
      hr_status: hr !== null ? 'live' : 'unavailable',
      hr_age_s: hr !== null ? 0 : null,
      skin_temp_c: temp,
      activity,
      x_m: Math.round(east * 10) / 10,
      y_m: Math.round(north * 10) / 10,
      sector: null,
      hops: 0,
      rssi: -58 - m.id * 4,
      psi_est: psiEst,
      psi_band: psiEst !== null ? band(psiEst) : null,
      baseline: { hr_bpm: m.hr0, skin_temp_c: m.temp0 },
      seconds_since_seen: Math.round((t - seenAt) * 10) / 10,
      packets: 1 + Math.floor(Math.min(t, silent ? m.silentAfter! : t) / 30) + (injured ? 1 : 0),
      trail: [],
    };
  }
}
