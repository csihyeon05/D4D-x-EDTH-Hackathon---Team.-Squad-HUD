export type Severity = 'OK' | 'WATCH' | 'WARN' | 'CRITICAL' | 'UNKNOWN';

/** Phone location bound to a soldier (POST /api/gps). Not the node's local x/y. */
export interface PhoneGpsFix {
  device_id: string;
  device_name: string | null;
  soldier_id: number | null;
  lat: number;
  lon: number;
  accuracy_m: number;
  alt_m: number | null;
  speed_mps: number | null;
  fix_unix_ms: number;
  recv_unix_ms: number;
  heading_deg: number | null;
  heading_source: 'gps' | 'compass' | 'none';
  id_conflict: boolean;
  age_s: number;
  /** [lat, lon] pairs, oldest first. */
  track: [number, number][];
}

export interface Soldier {
  gps: PhoneGpsFix | null;
  soldier_id: number;
  callsign: string;
  severity: Severity;
  stale: boolean;
  acked: boolean;
  active_code: number;
  active_label: string;
  interpretation: string;
  /** Current received measurement; null when unavailable or stale. Use for assessment. */
  hr_bpm: number | null;
  /** Display only: current value, or the last nonzero value for up to 8 s while reacquiring. */
  hr_display_bpm: number | null;
  hr_status: 'live' | 'reacquiring' | 'unavailable';
  /** Seconds since the last nonzero heart-rate packet. */
  hr_age_s: number | null;
  skin_temp_c: number | null;
  activity: number | null;
  x_m: number | null;
  y_m: number | null;
  sector: string | null;
  hops: number | null;
  rssi: number | null;
  psi_est: number | null;
  psi_band: string | null;
  baseline: { hr_bpm: number | null; skin_temp_c: number | null };
  seconds_since_seen: number | null;
  packets: number;
  trail: [number, number][];
}

export interface Alert {
  soldier_id: number;
  callsign: string;
  code: number;
  label: string;
  interpretation: string;
  severity: Severity;
  sector: string | null;
  t_s: number;
  hr_bpm: number | null;
  hops: number;
  rssi: number | null;
}

export interface SquadEvent {
  kind: string;
  sector: string;
  soldier_ids: number[];
  t_s: number;
  detail: string;
}

export interface Briefing {
  text: string;
  source: 'llm' | 'template';
  model: string | null;
  latency_ms: number;
  trigger: string;
  mission_time_s: number;
  note: string | null;
}

export interface C2State {
  gps: { devices: PhoneGpsFix[]; csv: string };
  mission_time_s: number;
  soldiers: Soldier[];
  alerts: Alert[];
  squad_events: SquadEvent[];
  counts: Partial<Record<Severity, number>>;
  stats: { packets: number; bytes: number; rejected: number; alerts: number; checkins: number };
  grid: { size_m: number; n: number };
  source: 'sim' | 'serial';
  emission: { sensor_samples: number; packets_on_air: number; radio_silence_pct: number } | null;
  briefings: Briefing[];
  llm: { enabled: boolean; reachable: boolean | null; model: string; base_url: string };
}

// ------------------------------------------------------------------ /example page
export interface ExampleRec {
  action: string;
  label: string;
  targets: string[];
  sector: string | null;
  reason: string;
  effect: string;
  source: 'llm' | 'rules';
}

export interface ExampleBrief {
  text: string;
  source: 'llm' | 'template';
  recommendations: ExampleRec[];
  recommendation_source: 'llm' | 'llm+rules' | 'rules';
  rejected: string[];
  model: string | null;
  latency_ms: number;
  mission_time_s: number;
  note: string | null;
}

export interface ExampleSoldier extends Soldier {
  subject: string;
  mode: string;
  resting_hr: number;
  hr_hist: number[];
  temp_overlay_c: number;
}

export interface ExampleMetrics {
  t_s: number;
  critical: number;
  warn: number;
  flagged: string[];
  mean_hr_flagged: number | null;
  in_hostile_sector: number | null;
  casualty_to_cp_m: number | null;
}

export interface ExampleState {
  phase: 'ready' | 'patrol' | 'incident' | 'brief' | 'solution' | 'outcome';
  scenario: string | null;
  scenario_info: { title: string; summary: string; expect: string } | null;
  t_s: number;
  speed: number;
  soldiers: ExampleSoldier[];
  alerts: Alert[];
  squad_events: SquadEvent[];
  counts: Partial<Record<Severity, number>>;
  stats: { packets: number; bytes: number; rejected: number; alerts: number; checkins: number };
  timeline: { t_s: number; kind: string; text: string }[];
  brief: ExampleBrief | null;
  executed: { action: string; label: string; targets: string[]; sector: string | null }[];
  outcome: {
    before: ExampleMetrics; after: ExampleMetrics;
    hr_change: { callsign: string; at_execute: number; now: number; rest: number }[];
    time_to_first_alert_s: number | null; time_alert_to_action_s: number | null; summary: string;
  } | null;
  hostile_sector: string | null;
  origin: { lat: number; lon: number };
  solution_progress: number | null;
  llm: { enabled: boolean; reachable: boolean | null; model: string; api: string };
}

export interface ExamplePersonas {
  ok: boolean;
  error?: string;
  source: { title: string; version: string; authors: string; publisher: string; year: number; doi: string;
    url: string; license: string; device: string };
  personas: { callsign: string; subject: string; sex: string; age: number; height_cm: number; weight_kg: number;
    regular_activity: boolean; resting_hr: number; resting_temp_c: number; max_hr: number }[];
  scenarios: Record<string, { title: string; summary: string; expect: string }>;
  provenance: { measured: string[]; modelled: string[] };
}
