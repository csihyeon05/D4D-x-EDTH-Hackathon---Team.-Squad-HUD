import { Component, computed, input, output, signal } from '@angular/core';
import { DecimalPipe } from '@angular/common';
import { Soldier } from './models';

/**
 * Map of phone GPS fixes: soldiers as dots on a metre grid that fits itself
 * around the squad, over satellite imagery (Esri World Imagery tiles).
 * Without internet the tiles simply do not load and the grid remains, so the
 * map still works offline; the Grid toggle hides the imagery on purpose.
 * The node's local dead-reckoning x/y is a different frame and is not drawn here.
 */

const W = 800;
const H = 520;
const PAD_PX = 48;
const MIN_HALF_SPAN_M = 40;
const GRID_STEPS_M = [5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000, 10000];
const OLD_FIX_S = 10;  // backend attaches fixes up to 10 s old

// Satellite basemap: Web Mercator XYZ tiles. Attribution is shown in the footer.
const TILE_URL = (z: number, x: number, y: number) =>
  `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/${z}/${y}/${x}`;
const TILE_ATTRIBUTION = 'Imagery © Esri, Maxar, Earthstar Geographics, GIS User Community';
const TILE_MAX_Z = 19;
const TILE_MAX_COUNT = 64;
const BASEMAP_KEY = 'cop_basemap';
type Basemap = 'satellite' | 'grid';

interface Tile { url: string; x: number; y: number; w: number; h: number; }

/** Fractional Web Mercator tile coordinates and their inverse. */
const tileX = (lon: number, n: number) => ((lon + 180) / 360) * n;
const tileY = (lat: number, n: number) => {
  const r = (lat * Math.PI) / 180;
  return ((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2) * n;
};
const tileLon = (x: number, n: number) => (x / n) * 360 - 180;
const tileLat = (y: number, n: number) => (Math.atan(Math.sinh(Math.PI * (1 - (2 * y) / n))) * 180) / Math.PI;

function loadBasemap(): Basemap {
  try { return localStorage.getItem(BASEMAP_KEY) === 'grid' ? 'grid' : 'satellite'; } catch { return 'satellite'; }
}

interface Placed {
  soldier: Soldier;
  x: number;
  y: number;
  accuracyPx: number;
  heading: { x: number; y: number } | null;
  track: string;
  oldFix: boolean;
}

@Component({
  selector: 'app-gps-map',
  imports: [DecimalPipe],
  template: `
    @let v = view();
    <svg [attr.viewBox]="'0 0 ' + width + ' ' + height" class="gps-svg" role="img" aria-label="Soldier positions from phone GPS"
         [class.satellite]="basemap() === 'satellite'" (click)="pick.emit(null)">
      @if (basemap() === 'satellite') {
        <g class="basemap">
          @for (t of v.tiles; track t.url) {
            <image [attr.href]="t.url" [attr.x]="t.x" [attr.y]="t.y" [attr.width]="t.w" [attr.height]="t.h"
                   preserveAspectRatio="none" />
          }
          <rect x="0" y="0" [attr.width]="width" [attr.height]="height" class="basemap-scrim" />
        </g>
      }
      @for (g of v.gridX; track g) {
        <line [attr.x1]="g" [attr.x2]="g" y1="0" [attr.y2]="height" class="grid-line" />
      }
      @for (g of v.gridY; track g) {
        <line x1="0" [attr.x2]="width" [attr.y1]="g" [attr.y2]="g" class="grid-line" />
      }

      @for (p of v.placed; track p.soldier.soldier_id) {
        <polyline [attr.points]="p.track" class="track" [attr.data-sev]="p.soldier.severity" />
        <circle [attr.cx]="p.x" [attr.cy]="p.y" [attr.r]="p.accuracyPx" class="accuracy" [attr.data-sev]="p.soldier.severity" />
      }
      @for (p of v.placed; track p.soldier.soldier_id) {
        <g class="unit" [attr.data-sev]="p.soldier.severity" [class.selected]="p.soldier.soldier_id === selected()"
           [class.old-fix]="p.oldFix" [class.dim]="selected() !== null && p.soldier.soldier_id !== selected()"
           [attr.transform]="'translate(' + p.x + ',' + p.y + ')'"
           (click)="$event.stopPropagation(); pick.emit(p.soldier.soldier_id)">
          <circle r="22" class="hit" />
          @if (p.soldier.severity === 'CRITICAL') { <circle r="18" class="pulse" /> }
          @if (p.soldier.soldier_id === selected()) { <circle r="15" class="ring" /> }
          @if (p.heading; as h) { <line x1="0" y1="0" [attr.x2]="h.x" [attr.y2]="h.y" class="heading" /> }
          <circle r="9" class="dot" />
          <text y="-16" text-anchor="middle" class="unit-label">{{ p.soldier.callsign }}</text>
        </g>
      }

      <g [attr.transform]="'translate(' + (width - 36) + ',44)'" class="north">
        <path d="M0,-24 L9,2 L0,-5 L-9,2 Z" />
        <text y="20" text-anchor="middle">N</text>
      </g>
      @if (v.placed.length) {
        <g [attr.transform]="'translate(24,' + (height - 24) + ')'" class="scale">
          <line x1="0" y1="0" [attr.x2]="v.scaleBarPx" y2="0" />
          <line x1="0" y1="-5" x2="0" y2="5" /><line [attr.x1]="v.scaleBarPx" y1="-5" [attr.x2]="v.scaleBarPx" y2="5" />
          <text [attr.x]="v.scaleBarPx / 2" y="-9" text-anchor="middle">{{ v.stepM }} m</text>
        </g>
      } @else {
        <text [attr.x]="width / 2" [attr.y]="height / 2" text-anchor="middle" class="empty">Waiting for phone GPS fixes</text>
      }
    </svg>
    <div class="map-foot mono">
      @if (v.center; as c) {
        <span>Centre {{ c.lat | number: '1.5-5' }}, {{ c.lon | number: '1.5-5' }}</span>
        <span>Grid {{ v.stepM }} m</span>
      }
      <span>Positions: phone GPS. Ring = GPS accuracy, tick = heading.</span>
      <button type="button" class="basemap-toggle" (click)="toggleBasemap()"
              [attr.aria-pressed]="basemap() === 'satellite'">{{ basemap() === 'satellite' ? 'Satellite' : 'Grid only' }}</button>
      @if (basemap() === 'satellite' && v.tiles.length) { <span class="attribution">{{ attribution }}</span> }
      @if (v.noGps.length) {
        <span class="no-gps">No GPS: {{ v.noGps.join(', ') }}</span>
      }
    </div>
  `,
})
export class GpsMap {
  readonly soldiers = input<Soldier[]>([]);
  readonly selected = input<number | null>(null);
  readonly pick = output<number | null>();

  protected readonly width = W;
  protected readonly height = H;
  protected readonly attribution = TILE_ATTRIBUTION;
  protected readonly basemap = signal<Basemap>(loadBasemap());

  protected toggleBasemap(): void {
    const next: Basemap = this.basemap() === 'satellite' ? 'grid' : 'satellite';
    this.basemap.set(next);
    try { localStorage.setItem(BASEMAP_KEY, next); } catch { /* per-viewer preference only */ }
  }

  protected readonly view = computed(() => {
    const all = this.soldiers();
    const located = all.filter((s) => s.gps !== null);
    const noGps = all.filter((s) => s.gps === null).map((s) => s.callsign);
    if (!located.length) {
      return { placed: [] as Placed[], tiles: [] as Tile[], gridX: [] as number[], gridY: [] as number[], stepM: 0, scaleBarPx: 0, center: null, noGps };
    }

    // Local metres around the squad centre (equirectangular; fine at squad scale).
    const lat0 = located.reduce((sum, s) => sum + s.gps!.lat, 0) / located.length;
    const lon0 = located.reduce((sum, s) => sum + s.gps!.lon, 0) / located.length;
    const mLat = 110_540;
    const mLon = 111_320 * Math.cos((lat0 * Math.PI) / 180);
    const toM = (lat: number, lon: number) => ({ e: (lon - lon0) * mLon, n: (lat - lat0) * mLat });

    let maxE = MIN_HALF_SPAN_M, maxN = MIN_HALF_SPAN_M;
    for (const s of located) {
      const g = s.gps!;
      for (const [lat, lon] of [...g.track, [g.lat, g.lon] as [number, number]]) {
        const p = toM(lat, lon);
        maxE = Math.max(maxE, Math.abs(p.e) + g.accuracy_m);
        maxN = Math.max(maxN, Math.abs(p.n) + g.accuracy_m);
      }
    }
    const scale = Math.min((W / 2 - PAD_PX) / maxE, (H / 2 - PAD_PX) / maxN);  // px per metre
    const px = (p: { e: number; n: number }) => ({ x: W / 2 + p.e * scale, y: H / 2 - p.n * scale });

    const stepM = GRID_STEPS_M.find((step) => step * scale >= 70) ?? GRID_STEPS_M[GRID_STEPS_M.length - 1];
    const gridX: number[] = [], gridY: number[] = [];
    for (let k = -Math.ceil(W / 2 / (stepM * scale)); k <= Math.ceil(W / 2 / (stepM * scale)); k++) gridX.push(W / 2 + k * stepM * scale);
    for (let k = -Math.ceil(H / 2 / (stepM * scale)); k <= Math.ceil(H / 2 / (stepM * scale)); k++) gridY.push(H / 2 + k * stepM * scale);

    const placed: Placed[] = located.map((s) => {
      const g = s.gps!;
      const here = px(toM(g.lat, g.lon));
      const rad = g.heading_deg !== null ? (g.heading_deg * Math.PI) / 180 : null;
      return {
        soldier: s,
        x: here.x,
        y: here.y,
        accuracyPx: Math.max(8, g.accuracy_m * scale),
        heading: rad !== null ? { x: Math.sin(rad) * 28, y: -Math.cos(rad) * 28 } : null,
        track: g.track.map(([lat, lon]) => { const q = px(toM(lat, lon)); return `${q.x},${q.y}`; }).join(' '),
        oldFix: g.age_s > OLD_FIX_S,
      };
    });
    // Satellite tiles covering the viewport, placed in the same local frame as the markers.
    const tiles: Tile[] = [];
    const mPerPxZ0 = (156_543.03392 * Math.cos((lat0 * Math.PI) / 180));   // Web Mercator m/px at zoom 0
    const z = Math.max(1, Math.min(TILE_MAX_Z, Math.round(Math.log2(mPerPxZ0 * scale))));
    const n = 2 ** z;
    const toLatLon = (x: number, y: number) => ({ lat: lat0 + (H / 2 - y) / scale / mLat, lon: lon0 + (x - W / 2) / scale / mLon });
    const nw = toLatLon(0, 0), se = toLatLon(W, H);
    const tx0 = Math.floor(tileX(nw.lon, n)), tx1 = Math.floor(tileX(se.lon, n));
    const ty0 = Math.floor(tileY(nw.lat, n)), ty1 = Math.floor(tileY(se.lat, n));
    if ((tx1 - tx0 + 1) * (ty1 - ty0 + 1) <= TILE_MAX_COUNT) {
      for (let ty = Math.max(0, ty0); ty <= Math.min(n - 1, ty1); ty++) {
        for (let tx = tx0; tx <= tx1; tx++) {
          const a = px(toM(tileLat(ty, n), tileLon(tx, n)));
          const b = px(toM(tileLat(ty + 1, n), tileLon(tx + 1, n)));
          const wrapped = ((tx % n) + n) % n;
          // +0.5 px overlap hides hairline seams between neighbouring tiles
          tiles.push({ url: TILE_URL(z, wrapped, ty), x: a.x, y: a.y, w: b.x - a.x + 0.5, h: b.y - a.y + 0.5 });
        }
      }
    }
    return { placed, tiles, gridX, gridY, stepM, scaleBarPx: stepM * scale, center: { lat: lat0, lon: lon0 }, noGps };
  });
}
