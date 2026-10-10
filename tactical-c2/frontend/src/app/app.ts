import { Component, OnInit, computed, inject, signal } from '@angular/core';
import { DecimalPipe } from '@angular/common';
import { C2Service } from './c2.service';
import { GpsMap } from './gps-map';
import { Severity, Soldier } from './models';

const SEV_RANK: Record<Severity, number> = { CRITICAL: 0, WARN: 1, UNKNOWN: 2, WATCH: 3, OK: 4 };
const IMMOBILE_ACTIVITY = 8;  // matches the node rules

/** What the commander should do for each CRITICAL alert code (tactical-c2/backend/app/protocol.py). */
const CRITICAL_ACTIONS: Record<number, { problem: string; action: string }> = {
  0x01: { problem: 'suspected injury: heart rate spiking and not moving', action: 'Send a medic and confirm status by radio.' },
  0x02: { problem: 'suspected loss of consciousness: low heart rate and not moving', action: 'Send a medic immediately and check breathing.' },
  0x03: { problem: 'suspected hit or fall: impact, then not moving', action: 'Send the nearest soldier and a medic to their position.' },
  0x12: { problem: 'requested immediate help (SOS)', action: 'Send support to their position and make radio contact.' },
};

export interface ActionItem {
  soldier: Soldier;
  problem: string;
  action: string;
  evidence: string;
}

@Component({
  selector: 'app-root',
  imports: [GpsMap, DecimalPipe],
  templateUrl: './app.html',
})
export class App implements OnInit {
  protected readonly c2 = inject(C2Service);
  protected readonly selected = signal<number | null>(null);

  /** Phone QR panel: the phone on each soldier's outer opens /gps?soldier=N over HTTPS. */
  protected readonly phoneOpen = signal(false);
  protected readonly phoneSoldier = signal<number>(1);
  protected readonly phoneLink = signal<{ url: string | null; ip: string | null; https: boolean } | null>(null);
  protected readonly phoneQrSrc = computed(() =>
    `${this.c2.httpBase}/api/phone-qr.svg?soldier=${this.phoneSoldier()}&t=${this.phoneStamp()}`);
  private readonly phoneStamp = signal(0);

  protected async togglePhone(): Promise<void> {
    const open = !this.phoneOpen();
    this.phoneOpen.set(open);
    if (open) {
      const first = this.c2.soldiers()[0]?.soldier_id;
      if (first && !this.c2.soldiers().some((s) => s.soldier_id === this.phoneSoldier())) this.phoneSoldier.set(first);
      await this.refreshPhone();
    }
  }

  protected async setPhoneSoldier(value: string): Promise<void> {
    const n = Number(value);
    if (Number.isInteger(n) && n >= 1 && n <= 255) {
      this.phoneSoldier.set(n);
      await this.refreshPhone();
    }
  }

  private async refreshPhone(): Promise<void> {
    this.phoneStamp.update((v) => v + 1);
    this.phoneLink.set(await this.c2.phoneLink(this.phoneSoldier()));
  }

  protected readonly scenarios = [
    { id: 'injury', label: 'Injury' },
    { id: 'unconscious', label: 'Unconscious' },
    { id: 'heat', label: 'Heat strain' },
    { id: 'ambush', label: 'Ambush' },
    { id: 'sos', label: 'SOS' },
    { id: 'silent', label: 'Node dark' },
  ];

  protected readonly roster = computed(() =>
    [...this.c2.soldiers()].sort(
      (a, b) => SEV_RANK[a.severity] - SEV_RANK[b.severity] || a.soldier_id - b.soldier_id,
    ),
  );

  protected readonly focus = computed<Soldier | null>(() => {
    const id = this.selected();
    return this.c2.soldiers().find((s) => s.soldier_id === id) ?? null;
  });

  /** Red soldiers, for the action banner. Built from alert data, so it works without the LLM. */
  protected readonly actions = computed<ActionItem[]>(() =>
    this.roster()
      .filter((s) => s.severity === 'CRITICAL')
      .map((s) => {
        const known = CRITICAL_ACTIONS[s.active_code];
        const evidence = [
          s.hr_bpm !== null ? `HR ${s.hr_bpm}` + (s.baseline.hr_bpm !== null ? ` (baseline ${Math.round(s.baseline.hr_bpm)})` : '') : 'no pulse reading',
          this.moving(s),
          s.gps ? `GPS ${s.gps.lat.toFixed(5)}, ${s.gps.lon.toFixed(5)}` : 'no GPS position',
          s.seconds_since_seen !== null ? `last packet ${this.mmss(s.seconds_since_seen)} ago` : '',
        ].filter(Boolean).join(' · ');
        return {
          soldier: s,
          problem: known?.problem ?? s.interpretation,
          action: known?.action ?? 'Check on them now.',
          evidence,
        };
      }),
  );

  /** The latest briefing, shown in the banner while anyone is red. */
  protected readonly commanderBriefing = computed(() => (this.actions().length ? this.c2.latestBriefing() : null));

  protected readonly clock = computed(() => `T+${this.mmss(this.c2.state()?.mission_time_s ?? 0)}`);

  ngOnInit(): void {
    this.c2.connect();
  }

  protected mmss(t: number): string {
    const s = Math.max(0, Math.floor(t));
    return `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
  }

  protected select(id: number | null): void {
    this.selected.update((cur) => (id === null || cur === id ? null : id));
  }

  /** Signed change from baseline, or null when either value is missing. */
  protected delta(value: number | null, base: number | null): number | null {
    return value === null || base === null ? null : value - base;
  }

  protected status(s: Soldier): string {
    if (s.stale) return 'No check-in';
    return s.active_code ? s.active_label : 'Nominal';
  }

  protected moving(s: Soldier): string {
    if (s.activity === null) return 'unknown';
    return s.activity < IMMOBILE_ACTIVITY ? 'not moving' : 'moving';
  }
}
