import { Component, OnDestroy, OnInit, computed, inject, signal } from '@angular/core';
import { DecimalPipe } from '@angular/common';
import { C2Service } from './c2.service';
import { GpsMap } from './gps-map';
import { ExampleBrief, ExamplePersonas, ExampleRec, ExampleSoldier, ExampleState } from './models';

const STEPS = [
  { id: 'personas', label: 'Personas' },
  { id: 'incident', label: 'Incident' },
  { id: 'brief', label: 'Operator brief' },
  { id: 'solution', label: 'Solution' },
  { id: 'outcome', label: 'Outcome' },
];
const PHASE_STEP: Record<string, number> = { ready: 0, patrol: 1, incident: 1, brief: 2, solution: 3, outcome: 4 };
const SEV_RANK: Record<string, number> = { CRITICAL: 0, WARN: 1, UNKNOWN: 2, WATCH: 3, OK: 4 };

/**
 * /example: public-dataset personas -> incident -> operator LLM brief + catalog actions
 * -> executed solution -> measured outcome. Runs on its own engine in the backend,
 * separate from the live COP picture.
 */
@Component({
  selector: 'app-root',
  imports: [GpsMap, DecimalPipe],
  template: `
    @let st = state();
    @let info = personas();
    <header class="topbar">
      <div class="brand">
        <span class="brand-mark"></span>
        <div>
          <div class="brand-title">SQUAD C2 · EXAMPLE RUN</div>
          <div class="brand-sub">Public-dataset personas, replayed through the real packet path</div>
        </div>
      </div>
      <ol class="ex-steps" aria-label="Progress">
        @for (s of steps; track s.id; let i = $index) {
          <li [class.done]="i < step()" [class.now]="i === step()"><span class="mono">{{ i + 1 }}</span>{{ s.label }}</li>
        }
      </ol>
      <div class="status">
        @if (st) {
          <span class="chip mono">T+{{ mmss(st.t_s) }}</span>
          <span class="chip" [class.chip-ok]="st.llm.reachable === true" [class.chip-warn]="st.llm.reachable === false">
            {{ st.llm.enabled ? 'sLLM ' + st.llm.model : 'sLLM OFF' }}</span>
        }
        <button type="button" class="chip chip-button" (click)="reset()">RESET</button>
      </div>
    </header>

    @if (error()) { <div class="ex-error" role="alert">{{ error() }}</div> }

    @if (!st || st.phase === 'ready') {
      <main class="ex-setup">
        <section class="panel">
          <div class="panel-head"><h2>1 · Personas</h2>
            @if (info?.ok) { <span class="muted small">{{ info!.personas.length }} subjects → soldiers S1–S{{ info!.personas.length }}</span> }
          </div>
          @if (info && !info.ok) {
            <p class="warn-text">{{ info.error }}</p>
          } @else if (info) {
            <div class="ex-table-wrap">
              <table class="ex-table mono">
                <thead><tr><th>Soldier</th><th>Subject</th><th>Sex</th><th>Age</th><th>Height</th><th>Weight</th>
                  <th>Regular activity</th><th>Resting HR</th><th>Max HR</th><th>Resting skin</th></tr></thead>
                <tbody>
                  @for (p of info.personas; track p.callsign) {
                    <tr><td><b>{{ p.callsign }}</b></td><td>{{ p.subject }}</td><td>{{ p.sex }}</td><td>{{ p.age }}</td>
                      <td>{{ p.height_cm }} cm</td><td>{{ p.weight_kg }} kg</td><td>{{ p.regular_activity ? 'yes' : 'no' }}</td>
                      <td>{{ p.resting_hr }}</td><td>{{ p.max_hr }}</td><td>{{ p.resting_temp_c | number: '1.1-1' }} °C</td></tr>
                  }
                </tbody>
              </table>
            </div>
            <p class="ex-cite small">
              Data: {{ info.source.authors }}. <i>{{ info.source.title }}</i> (version {{ info.source.version }}).
              {{ info.source.publisher }}, {{ info.source.year }}.
              <a [href]="'https://doi.org/' + info.source.doi" target="_blank" rel="noopener">doi:{{ info.source.doi }}</a>.
              {{ info.source.license }}. Device: {{ info.source.device }}.
            </p>
            <details class="ex-prov">
              <summary>What is measured and what is modelled</summary>
              <div class="ex-prov-cols">
                <div><h3>Measured (dataset)</h3><ul>@for (m of info.provenance.measured; track m) { <li>{{ m }}</li> }</ul></div>
                <div><h3>Modelled (labelled)</h3><ul>@for (m of info.provenance.modelled; track m) { <li>{{ m }}</li> }</ul></div>
              </div>
            </details>
          } @else {
            <p class="muted">Loading personas…</p>
          }
        </section>

        @if (info?.ok) {
          <section class="panel">
            <div class="panel-head"><h2>2 · Choose the incident</h2>
              <label class="small muted">Speed
                <select class="ex-select" [value]="speed()" (change)="speed.set(+$any($event.target).value)">
                  <option value="1">1×</option><option value="2">2×</option><option value="4">4×</option>
                </select>
              </label>
            </div>
            <div class="ex-cards">
              @for (sc of scenarioList(); track sc.id) {
                <article class="ex-card">
                  <h3>{{ sc.title }}</h3>
                  <p>{{ sc.summary }}</p>
                  <p class="mono small muted">Expected: {{ sc.expect }}</p>
                  <button class="btn btn-primary" [disabled]="busy()" (click)="start(sc.id)">Run simulation</button>
                </article>
              }
            </div>
          </section>
        }
      </main>
    } @else {
      <main class="ex-run">
        <section class="panel ex-map">
          <div class="panel-head">
            <h2>{{ st.scenario_info?.title }} · {{ phaseLabel(st.phase) }}</h2>
            <span class="muted small">Synthetic positions around {{ st.origin.lat | number: '1.4-4' }}, {{ st.origin.lon | number: '1.4-4' }}
              @if (st.hostile_sector) { · contact sector <b class="warn-text">{{ st.hostile_sector }}</b> }</span>
          </div>
          <app-gps-map [soldiers]="st.soldiers" [selected]="selected()" (pick)="selected.set($event)" />
          @if (st.solution_progress !== null && st.phase === 'solution') {
            <div class="ex-progress"><i [style.width.%]="st.solution_progress * 100"></i></div>
          }
        </section>

        <section class="panel ex-vitals">
          <div class="panel-head"><h2>Vitals (measured HR)</h2><span class="muted small">line = HR, dashed = resting</span></div>
          @for (s of roster(); track s.soldier_id) {
            <button type="button" class="ex-vrow" [attr.data-sev]="s.severity" [class.selected]="s.soldier_id === selected()"
                    (click)="selected.set(s.soldier_id)">
              <span class="ex-vwho"><b class="mono"><i class="sw" [attr.data-sev]="s.severity"></i>{{ s.callsign }}</b>
                <span class="muted small">{{ s.subject }} · {{ modeLabel(s.mode) }}</span>
                <span class="small ex-vstat">{{ s.active_code ? s.active_label : 'Nominal' }}</span></span>
              <svg class="spark" viewBox="0 0 120 34" preserveAspectRatio="none" aria-hidden="true">
                <line x1="0" x2="120" [attr.y1]="sparkY(s.resting_hr)" [attr.y2]="sparkY(s.resting_hr)" class="spark-rest" />
                <polyline [attr.points]="spark(s.hr_hist)" class="spark-line" />
              </svg>
              <span class="mono ex-vnum">{{ s.hr_bpm ?? '–' }}<small> bpm</small><br />
                <span class="muted">{{ s.skin_temp_c !== null ? (s.skin_temp_c | number: '1.1-1') + '°' : '–' }}
                  @if (s.temp_overlay_c > 0) { <small class="warn-text">+{{ s.temp_overlay_c | number: '1.1-1' }}m</small> }</span></span>
            </button>
          }
        </section>

        <section class="panel ex-operator">
          <div class="panel-head">
            <h2>3 · Operator</h2>
            <button class="btn btn-primary" (click)="ask()" [disabled]="busy() || !canAsk()">
              {{ asking() ? 'Operator is thinking…' : st.brief ? 'Ask again' : 'Ask operator' }}</button>
          </div>
          @if (!st.brief) {
            <p class="muted small">
              @if (st.phase === 'patrol') { Patrol in progress. The incident starts at T+00:50. }
              @else if (!st.alerts.length) { Incident started. Waiting for the first node alert… }
              @else { {{ st.alerts.length }} alert(s) received. Ask the operator for a brief and recommended actions. }
            </p>
          }
          @if (st.brief; as b) {
            <p class="briefing-text">{{ b.text }}</p>
            <div class="briefing-meta mono">
              <span class="tag" [class.tag-llm]="b.source === 'llm'">brief: {{ b.source === 'llm' ? b.model : 'rule template' }}</span>
              <span class="tag" [class.tag-llm]="b.recommendation_source !== 'rules'">actions: {{ b.recommendation_source === 'rules' ? 'rules' : b.model + (b.recommendation_source === 'llm+rules' ? ' + rules' : '') }}</span>
              <span>{{ b.latency_ms }} ms</span><span>facts at T+{{ mmss(b.mission_time_s) }}</span>
            </div>
            @if (b.note) { <div class="briefing-note">{{ b.note }}</div> }
            <h3>Recommended actions (fixed catalog)</h3>
            <ul class="ex-recs">
              @for (r of b.recommendations; track $index; let i = $index) {
                <li [class.off]="!chosen()[i]">
                  <label>
                    <input type="checkbox" [checked]="chosen()[i]" [disabled]="st.phase === 'solution' || st.phase === 'outcome'"
                           (change)="toggle(i)" />
                    <span class="ex-rec-head"><b>{{ i + 1 }}. {{ r.label }}</b>
                      <span class="mono">{{ r.targets.join(', ') }}{{ r.sector ?? '' }}</span>
                      <span class="tag mono small" [class.tag-llm]="r.source === 'llm'">{{ r.source === 'llm' ? 'LLM pick' : b.recommendation_source === 'rules' ? 'rule' : 'rule (gap fill)' }}</span></span>
                  </label>
                  @if (r.reason) { <span class="small">{{ r.reason }}</span> }
                  <span class="small muted">{{ r.effect }}</span>
                </li>
              }
            </ul>
            @if (b.rejected.length) {
              <p class="small muted">Validator notes: {{ b.rejected.join('; ') }}</p>
            }
            @if (st.phase === 'incident' || st.phase === 'brief') {
              <button class="btn btn-danger ex-exec" (click)="execute(b)" [disabled]="busy() || !anyChosen()">4 · Execute selected actions</button>
            }
          }
        </section>

        <section class="panel ex-timeline">
          <div class="panel-head"><h2>Timeline</h2><span class="muted small">{{ st.stats.packets }} packets · {{ st.stats.alerts }} alerts</span></div>
          <ol class="ex-tl">
            @for (e of timelineDesc(); track $index) {
              <li [attr.data-kind]="e.kind"><span class="mono muted">T+{{ mmss(e.t_s) }}</span><span class="ex-kind mono">{{ e.kind }}</span><span>{{ e.text }}</span></li>
            }
          </ol>
        </section>

        @if (st.outcome; as o) {
          <section class="panel ex-outcome">
            <div class="panel-head"><h2>5 · Outcome</h2><span class="muted small">{{ executedText() }}</span></div>
            <table class="ex-table mono">
              <thead><tr><th></th><th>At execute (T+{{ mmss(o.before.t_s) }})</th><th>After (T+{{ mmss(o.after.t_s) }})</th></tr></thead>
              <tbody>
                <tr><td>Critical soldiers</td><td>{{ o.before.critical }}</td><td>{{ o.after.critical }}</td></tr>
                <tr><td>Warn soldiers</td><td>{{ o.before.warn }}</td><td>{{ o.after.warn }}</td></tr>
                <tr><td>Flagged</td><td>{{ o.before.flagged.join(', ') || '–' }}</td><td>{{ o.after.flagged.join(', ') || '–' }}</td></tr>
                <tr><td>Mean HR of flagged</td><td>{{ o.before.mean_hr_flagged ?? '–' }}</td><td>{{ o.after.mean_hr_flagged ?? '–' }}</td></tr>
                @if (o.before.in_hostile_sector !== null) {
                  <tr><td>Soldiers in contact sector</td><td>{{ o.before.in_hostile_sector }}</td><td>{{ o.after.in_hostile_sector }}</td></tr>
                }
                @if (o.before.casualty_to_cp_m !== null) {
                  <tr><td>Casualty distance to CP</td><td>{{ o.before.casualty_to_cp_m }} m</td><td>{{ o.after.casualty_to_cp_m }} m</td></tr>
                }
              </tbody>
            </table>
            @if (o.hr_change.length) {
              <p class="small">HR of action targets (measured recovery):
                @for (h of o.hr_change; track h.callsign) { <span class="mono">{{ h.callsign }} {{ h.at_execute }} → {{ h.now }} (rest {{ h.rest }})</span>{{ $last ? '' : ' · ' }} }
              </p>
            }
            <p class="small muted">Incident → first node alert: {{ o.time_to_first_alert_s ?? '–' }} s ·
              first alert → executed action: {{ o.time_alert_to_action_s ?? '–' }} s (simulation time)</p>
            <button class="btn" (click)="reset()">Run another scenario</button>
          </section>
        }
      </main>
    }
  `,
})
export class ExamplePage implements OnInit, OnDestroy {
  private readonly base = inject(C2Service).httpBase;
  protected readonly steps = STEPS;
  protected readonly state = signal<ExampleState | null>(null);
  protected readonly personas = signal<ExamplePersonas | null>(null);
  protected readonly selected = signal<number | null>(null);
  protected readonly speed = signal(2);
  protected readonly busy = signal(false);
  protected readonly asking = signal(false);
  protected readonly error = signal<string | null>(null);
  protected readonly chosen = signal<boolean[]>([]);
  private timer?: ReturnType<typeof setInterval>;
  private briefStamp: number | null = null;

  protected readonly step = computed(() => PHASE_STEP[this.state()?.phase ?? 'ready'] ?? 0);
  protected readonly roster = computed<ExampleSoldier[]>(() =>
    [...(this.state()?.soldiers ?? [])].sort((a, b) => SEV_RANK[a.severity] - SEV_RANK[b.severity] || a.soldier_id - b.soldier_id));
  protected readonly scenarioList = computed(() =>
    Object.entries(this.personas()?.scenarios ?? {}).map(([id, s]) => ({ id, ...s })));
  protected readonly timelineDesc = computed(() => [...(this.state()?.timeline ?? [])].reverse());
  protected readonly canAsk = computed(() => {
    const st = this.state();
    return !!st && (st.phase === 'incident' || st.phase === 'brief') && st.alerts.length > 0;
  });
  protected readonly anyChosen = computed(() => this.chosen().some(Boolean));
  protected readonly executedText = computed(() =>
    (this.state()?.executed ?? []).map((d) => d.label + (d.targets.length ? ' ' + d.targets.join(', ') : '') + (d.sector ? ' ' + d.sector : '')).join(' · '));

  async ngOnInit(): Promise<void> {
    document.title = 'Squad C2 · Example';
    try {
      this.personas.set(await this.get<ExamplePersonas>('/api/example/personas'));
    } catch {
      this.error.set('Backend not reachable.');
    }
    await this.poll();
    this.timer = setInterval(() => this.poll(), 1000);
  }

  ngOnDestroy(): void {
    clearInterval(this.timer);
  }

  private async poll(): Promise<void> {
    try {
      const st = await this.get<ExampleState>('/api/example/state');
      this.state.set(st);
      const stamp = st.brief ? st.brief.mission_time_s + st.brief.latency_ms : null;
      if (stamp !== this.briefStamp) {
        this.briefStamp = stamp;
        this.chosen.set((st.brief?.recommendations ?? []).map(() => true));
      }
    } catch { /* keep the last state; the next poll retries */ }
  }

  protected async start(scenario: string): Promise<void> {
    await this.act(() => this.post('/api/example/start', { scenario, speed: this.speed() }));
  }

  protected async ask(): Promise<void> {
    this.asking.set(true);
    await this.act(() => this.post('/api/example/brief', {}));
    this.asking.set(false);
  }

  protected async execute(b: ExampleBrief): Promise<void> {
    const actions: ExampleRec[] = b.recommendations.filter((_, i) => this.chosen()[i]);
    await this.act(() => this.post('/api/example/solution', { actions }));
  }

  protected async reset(): Promise<void> {
    this.selected.set(null);
    await this.act(() => this.post('/api/example/reset', {}));
  }

  protected toggle(i: number): void {
    this.chosen.update((c) => c.map((v, k) => (k === i ? !v : v)));
  }

  private async act(fn: () => Promise<unknown>): Promise<void> {
    this.busy.set(true);
    this.error.set(null);
    try {
      await fn();
      await this.poll();
    } catch (e) {
      this.error.set(e instanceof Error ? e.message : String(e));
    } finally {
      this.busy.set(false);
    }
  }

  private async get<T>(path: string): Promise<T> {
    const r = await fetch(this.base + path, { cache: 'no-store' });
    if (!r.ok) throw new Error(`${path}: HTTP ${r.status}`);
    return r.json() as Promise<T>;
  }

  private async post(path: string, body: unknown): Promise<unknown> {
    const r = await fetch(this.base + path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    if (!r.ok) {
      let detail = `HTTP ${r.status}`;
      try { detail = (await r.json()).detail ?? detail; } catch { /* not JSON */ }
      throw new Error(detail);
    }
    return r.json();
  }

  protected sparkY(hr: number): number {
    return 34 - ((Math.max(40, Math.min(200, hr)) - 40) / 160) * 32 - 1;
  }

  protected spark(hist: number[]): string {
    const n = hist.length;
    return hist.map((h, i) => `${(120 * i) / Math.max(1, n - 1)},${this.sparkY(h)}`).join(' ');
  }

  protected phaseLabel(p: string): string {
    return ({ patrol: 'patrol', incident: 'incident in progress', brief: 'operator briefed', solution: 'executing solution', outcome: 'outcome' } as Record<string, string>)[p] ?? p;
  }

  protected modeLabel(m: string): string {
    return ({ patrol: 'patrolling', down: 'down', heat: 'heavy work', rest: 'resting', aid: 'receiving aid', carried: 'being evacuated',
      carrier: 'evacuating casualty', move: 'moving', hold: 'holding' } as Record<string, string>)[m] ?? m;
  }

  protected mmss(t: number): string {
    const s = Math.max(0, Math.floor(t));
    return `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
  }
}
