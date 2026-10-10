import { Injectable, computed, signal } from '@angular/core';
import { Briefing, C2State } from './models';
import { MockSource } from './mock';

/** Backend location: same origin when served by FastAPI, :8000 during `ng serve`. */
function backendBase(): { http: string; ws: string } {
  const { protocol, hostname, port, host } = window.location;
  const h = port === '4200' ? `${hostname}:8000` : host;
  return { http: `${protocol}//${h}`, ws: `${protocol === 'https:' ? 'wss' : 'ws'}://${h}/ws` };
}

@Injectable({ providedIn: 'root' })
export class C2Service {
  private readonly base = backendBase();
  /** Backend origin for plain URLs (images, links). */
  readonly httpBase = this.base.http;
  private socket?: WebSocket;
  private retry?: ReturnType<typeof setTimeout>;
  /** `?mock` in the URL: run from front-end mock data, no backend needed. */
  private readonly mock = new URLSearchParams(window.location.search).has('mock') ? new MockSource() : null;
  readonly mockMode = this.mock !== null;

  readonly state = signal<C2State | null>(null);
  readonly connected = signal(false);
  readonly latestBriefing = signal<Briefing | null>(null);
  readonly briefingBusy = signal(false);

  readonly soldiers = computed(() => this.state()?.soldiers ?? []);
  readonly flagged = computed(() =>
    this.soldiers().filter((s) => s.severity !== 'OK').length,
  );

  connect(): void {
    if (this.mock) {
      const tick = () => {
        const state = this.mock!.state();
        this.state.set(state);
        if (state.briefings[0] && state.briefings[0] !== this.latestBriefing()) this.latestBriefing.set(state.briefings[0]);
      };
      tick();
      this.connected.set(true);
      setInterval(tick, 1000);
      return;
    }
    clearTimeout(this.retry);
    const ws = new WebSocket(this.base.ws);
    this.socket = ws;
    ws.onopen = () => this.connected.set(true);
    ws.onclose = () => {
      this.connected.set(false);
      this.retry = setTimeout(() => this.connect(), 1500);
    };
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.type === 'state') {
        this.state.set(msg.state);
        if (!this.latestBriefing() && msg.state.briefings?.length) {
          this.latestBriefing.set(msg.state.briefings[0]);
        }
      } else if (msg.type === 'briefing') {
        this.latestBriefing.set(msg.briefing);
      }
    };
  }

  /** Phone GPS link for a soldier (HTTPS address the phone can reach on the same Wi-Fi). */
  async phoneLink(soldier: number | null): Promise<{ url: string | null; ip: string | null; https: boolean } | null> {
    if (this.mock) return null;
    try {
      const q = soldier ? `?soldier=${soldier}` : '';
      const r = await fetch(`${this.base.http}/api/phone-link${q}`);
      return r.ok ? await r.json() : null;
    } catch { return null; }
  }

  private post(path: string, body?: unknown): Promise<Response> {
    return fetch(`${this.base.http}${path}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: body ? JSON.stringify(body) : undefined,
    });
  }

  async scenario(name: string): Promise<void> {
    if (this.mock) return;
    await this.post('/api/scenario', { name });
    if (name === 'reset') this.latestBriefing.set(null);
  }

  async ack(soldierId: number): Promise<void> {
    if (this.mock) {
      this.mock.ack(soldierId);
      this.state.set(this.mock.state());
      return;
    }
    await this.post(`/api/soldiers/${soldierId}/ack`);
  }

  async briefNow(): Promise<void> {
    if (this.mock) {
      this.latestBriefing.set(this.mock.brief());
      return;
    }
    this.briefingBusy.set(true);
    try {
      const r = await this.post('/api/briefing');
      this.latestBriefing.set(await r.json());
    } finally {
      this.briefingBusy.set(false);
    }
  }
}
