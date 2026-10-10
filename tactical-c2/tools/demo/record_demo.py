"""Record the submission demo video: COP walkthrough + /example run, with on-screen captions.

Starts its own backend (simulator source, separate port) so the live suit server is untouched,
feeds simulated phone-GPS fixes for the simulated squad, drives the pages with Playwright and
saves a WebM. Run from tactical-c2/backend with its venv:

    python ../tools/demo/record_demo.py --out ../../demo_video [--port 8010] [--no-llm]

Needs: pip install playwright && python -m playwright install chromium
"""
from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

from playwright.sync_api import Page, sync_playwright

ORIGIN = (37.8932, 127.0566)
W, H = 1600, 900

CAPTION_JS = """
(() => {
  if (document.getElementById('demo-cap')) return;
  const s = document.createElement('style');
  s.textContent = `
  #demo-cap{position:fixed;left:50%;bottom:28px;transform:translateX(-50%);z-index:99999;max-width:1180px;
    background:rgba(8,12,10,.92);color:#eef5ef;border:1px solid #9fd36b;border-radius:10px;padding:14px 22px;
    font:600 22px/1.4 system-ui,'Segoe UI','Noto Sans KR',sans-serif;box-shadow:0 8px 30px rgba(0,0,0,.5);
    transition:opacity .3s;pointer-events:none}
  #demo-cap small{display:block;font-weight:400;font-size:16px;color:#b9c8bd;margin-top:4px}
  #demo-step{position:fixed;left:18px;bottom:34px;z-index:99999;background:#9fd36b;color:#0d1210;
    font:700 14px/1 ui-monospace,Consolas,monospace;letter-spacing:.08em;padding:7px 10px;border-radius:4px;pointer-events:none}
  .demo-hl{outline:3px solid #ffd54a !important;outline-offset:3px;border-radius:6px;
    box-shadow:0 0 0 9999px rgba(0,0,0,.28) !important;position:relative;z-index:9000;transition:outline .2s}
  #demo-card{position:fixed;inset:0;z-index:100000;background:#0d1210;color:#eef5ef;display:flex;flex-direction:column;
    align-items:center;justify-content:center;text-align:center;font-family:system-ui,'Segoe UI','Noto Sans KR',sans-serif}
  #demo-card h1{font-size:46px;margin:0 0 14px;letter-spacing:.04em}
  #demo-card p{font-size:22px;color:#b9c8bd;margin:6px 0;max-width:1100px}
  #demo-card .acc{color:#9fd36b}`;
  document.head.appendChild(s);
  const c = document.createElement('div'); c.id = 'demo-cap'; c.style.opacity = 0; document.body.appendChild(c);
  const st = document.createElement('div'); st.id = 'demo-step'; st.style.display = 'none'; document.body.appendChild(st);
})()
"""


# ----------------------------------------------------------------------------- helpers
def http(base: str, path: str, body: dict | None = None) -> dict:
    req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"},
                                 method="GET" if body is None else "POST")
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())


def latlon(x: float, y: float) -> tuple[float, float]:
    return (ORIGIN[0] + (y - 150) / 110_540, ORIGIN[1] + (x - 150) / (111_320 * math.cos(math.radians(ORIGIN[0]))))


def gps_feeder(base: str, stop: threading.Event) -> None:
    """Simulated phone on each simulated soldier's outer: post a fix per soldier every second."""
    last: dict[int, tuple[float, float]] = {}
    while not stop.is_set():
        try:
            for s in http(base, "/api/state")["soldiers"]:
                if s["x_m"] is None:
                    continue
                lat, lon = latlon(s["x_m"], s["y_m"])
                prev = last.get(s["soldier_id"])
                heading, speed = None, 0.0
                if prev:
                    dn, de = (lat - prev[0]) * 110_540, (lon - prev[1]) * 111_320 * math.cos(math.radians(lat))
                    speed = math.hypot(dn, de)
                    heading = (math.degrees(math.atan2(de, dn)) + 360) % 360 if speed > 0.2 else None
                last[s["soldier_id"]] = (lat, lon)
                http(base, "/api/gps", {"soldier_id": s["soldier_id"], "device_id": f"sim-phone-{s['soldier_id']}",
                                        "device_name": "simulated phone", "fix_unix_ms": int(time.time() * 1000),
                                        "lat": lat, "lon": lon, "accuracy_m": 4.0, "speed_mps": speed,
                                        "heading_gps_deg": heading})
        except Exception:
            pass
        stop.wait(1.0)


class Director:
    def __init__(self, page: Page) -> None:
        self.p = page

    def setup(self) -> None:
        self.p.evaluate(CAPTION_JS)

    def cap(self, text: str, sub: str = "", hold: float = 0.0, top: bool = False) -> None:
        """Caption box; top=True when the highlighted panel sits low on the screen."""
        self.setup()
        self.p.evaluate("([t,s,top]) => { const c=document.getElementById('demo-cap'); c.innerHTML = t + (s ? '<small>'+s+'</small>' : '');"
                        " c.style.top = top ? '84px' : 'auto'; c.style.bottom = top ? 'auto' : '28px'; c.style.opacity = 1; }",
                        [text, sub, top])
        if hold:
            self.p.wait_for_timeout(int(hold * 1000))

    def step(self, label: str) -> None:
        self.setup()
        self.p.evaluate("t => { const s=document.getElementById('demo-step'); s.textContent=t; s.style.display = t ? 'block' : 'none'; }", label)

    def hl(self, selector: str | None) -> None:
        self.p.evaluate("sel => { document.querySelectorAll('.demo-hl').forEach(e=>e.classList.remove('demo-hl'));"
                        " if (sel) { const e=document.querySelector(sel); if (e) { e.classList.add('demo-hl'); e.scrollIntoView({block:'center', behavior:'smooth'}); } } }",
                        selector)

    def card(self, title: str, lines: list[str], hold: float) -> None:
        self.setup()
        self.p.evaluate("([t, ls]) => { let d=document.getElementById('demo-card'); if(!d){d=document.createElement('div'); d.id='demo-card'; document.body.appendChild(d);} "
                        "d.innerHTML='<h1>'+t+'</h1>'+ls.map(l=>'<p>'+l+'</p>').join(''); d.style.display='flex'; }", [title, lines])
        self.p.wait_for_timeout(int(hold * 1000))
        self.p.evaluate("() => { const d=document.getElementById('demo-card'); if (d) d.style.display='none'; }")

    def wait_until(self, js: str, timeout: float) -> bool:
        try:
            self.p.wait_for_function(js, timeout=int(timeout * 1000))
            return True
        except Exception:
            return False


# ----------------------------------------------------------------------------- scenes
def cop_scenes(d: Director, base: str) -> None:
    p = d.p
    p.goto(base + "/")
    p.wait_for_timeout(2500)
    d.card("SQUAD C2 · Biometric Offline Squad Command",
           ["Track 4 · Tactical Edge — D4D × EDTH Korean Defense Tech Hackathon",
            "Soldier-worn sensor nodes → NU40 gateway → offline COP + local sLLM operator",
            '<span class="acc">Demo recording · simulated squad of 8 (same packet path as the real suit)</span>'], 6)

    d.step("1 · COP")
    d.hl(None)
    d.cap("Commander's Common Operational Picture (COP) — runs fully offline on the laptop",
          "Each soldier node stays radio-silent and transmits 18-byte packets only on check-ins and alerts", 6)
    d.hl(".kpis")
    d.cap("Mission clock, flagged soldiers, packets received and radio-silence ratio",
          "Radio silence = share of sensor samples that never went on air (low electronic signature)", 6)
    d.hl(".status")
    d.cap("Link status: gateway / simulator source, and the local sLLM operator (Ollama, offline)", "", 5)
    d.hl(".map-panel")
    d.cap("Squad positions from the phone GPS mounted on each soldier's outer garment",
          "Satellite basemap; ring = GPS accuracy, tick = heading, dashed line = recent track", 7)
    p.click("button.basemap-toggle")
    d.cap("Satellite / grid toggle — without internet the metre grid keeps working", "", 3.5)
    p.click("button.basemap-toggle")
    p.wait_for_timeout(1500)
    d.hl(".vitals-panel")
    d.cap("Live vitals per soldier: heart rate, skin temperature, strain index (PSI), activity, last seen",
          "A node not heard for 10 s is shown as UNKNOWN (lost)", 6)
    p.locator("button.vt-row").nth(1).click()
    p.wait_for_timeout(500)
    d.hl(".focus")
    d.cap("Soldier detail: change vs. personal baseline, GPS fix, dead-reckoning position and radio quality", "", 7)

    d.hl(None)
    p.click("text=PHONE QR")
    p.wait_for_timeout(800)
    d.hl("#phone-panel")
    d.cap("PHONE QR — scan to bind the phone on a soldier's outer to that soldier ID",
          "The phone page streams GPS over HTTPS on the same Wi-Fi / hotspot", 7)
    p.click("button.phone-close")
    d.hl(None)

    d.step("2 · ALERT → BRIEF")
    p.click("footer.controls >> text=Ambush")
    d.cap("Scenario: an ambush — three soldiers go prone, heart rate spikes, one presses CONTACT",
          "Nodes decide on-board when to break radio silence (HR spike + no movement ≥ 10 s)", 4)
    d.wait_until("() => document.querySelector('.action-banner') !== null", 40)
    d.hl(".action-banner")
    d.cap("Action required: critical soldiers with the problem, the action and the evidence",
          "Built from the alert data itself, so it works even if the LLM is unavailable", 8)
    d.wait_until("() => document.querySelector('.briefing-panel .briefing-text') !== null", 40)
    d.hl(".briefing-panel")
    d.cap("AI briefing from the local operator model — grounded on verified facts only",
          "If the model names an unknown soldier or a condition not in the alerts, the rule template is shown instead", 9, top=True)
    d.hl(".alerts-panel")
    d.cap("Alert log and squad-level correlation: several soldiers close together breaking silence = POSSIBLE CONTACT", "", 7, top=True)
    d.hl(".action-banner")
    btn = p.locator(".action-banner button.btn-danger").first
    if btn.count():
        btn.click()
    d.cap("The commander acknowledges — the alert stays visible as handled", "", 4)
    d.hl(None)


def example_scenes(d: Director, base: str, scenario: str) -> None:
    p = d.p
    d.step("3 · EXAMPLE")
    d.hl("a.chip-button[href='/example']")
    d.cap("EXAMPLE — public-dataset personas → incident → operator brief → solution → outcome",
          "Opens in a new tab and runs on a separate engine; the live COP is untouched", 5)
    p.goto(base + "/example")
    p.wait_for_timeout(2000)
    d.step("3 · EXAMPLE · 1 Personas")
    d.hl(".ex-table-wrap")
    d.cap("Eight soldiers driven by real wristband recordings of eight subjects",
          "PhysioNet Wearable Device Dataset v1.0.1 (Empatica E4, ODC-By 1.0) — heart rate, skin temperature, motion", 8)
    p.click(".ex-prov summary")
    p.wait_for_timeout(600)
    d.hl(".ex-prov")
    d.cap("Measured vs. modelled is labelled on the page", "Positions, falls and the heat overlay are modelled; vitals are measured", 7)
    d.hl(".ex-cards")
    d.cap("Choose the incident: contact, single casualty, or heat strain on the march", "", 5)
    p.select_option("select.ex-select", "4")
    p.locator(".ex-card").filter(has_text={"ambush": "Contact", "casualty": "Single casualty", "heat": "Heat"}[scenario]) \
        .locator("button").click()
    p.wait_for_timeout(1500)
    d.step("3 · EXAMPLE · 2 Incident")
    d.hl(".ex-map")
    d.cap("Patrol: recorded heart rate and skin temperature run through the node rules every second",
          "Every sample is encoded to the 18-byte radio packet and decoded again — same path as the hardware", 8)
    d.wait_until("() => document.querySelectorAll('.ex-tl li[data-kind=\"alert\"]').length >= 1", 60)
    d.hl(".ex-vitals")
    d.cap("Incident: nodes break radio silence — measured heart-rate jump with no movement", "", 7)
    d.wait_until("() => document.querySelectorAll('.ex-tl li[data-kind=\"alert\"]').length >= 3", 20)
    d.hl(".ex-timeline")
    d.cap("Timeline: node alerts and the squad-level POSSIBLE CONTACT event", "", 5, top=True)

    d.step("3 · EXAMPLE · 3 Operator")
    d.hl(".ex-operator")
    d.cap("Ask the operator: the local LLM writes the brief and picks actions from a fixed catalog", "", 3, top=True)
    p.click(".ex-operator button.btn-primary")
    d.wait_until("() => document.querySelector('.ex-recs') !== null", 90)
    p.wait_for_timeout(800)
    d.cap("Brief + recommended actions — each tagged LLM pick or rule gap-fill",
          "A validator drops unknown actions, soldiers, sectors and targets that do not meet the action's precondition", 14, top=True)

    d.step("3 · EXAMPLE · 4 Solution")
    p.click(".ex-exec")
    p.wait_for_timeout(1000)
    d.hl(".ex-map")
    d.cap("Execute: the nearest soldier reaches the casualty and carries them to the CP; the squad withdraws", "", 10)
    d.hl(".ex-vitals")
    d.cap("The casualty's heart rate follows the subject's own measured recovery", "", 6)
    d.wait_until("() => document.querySelector('.ex-outcome') !== null", 90)
    d.step("3 · EXAMPLE · 5 Outcome")
    d.hl(".ex-outcome")
    d.cap("Outcome, before vs. after: critical soldiers, soldiers in the contact sector, casualty distance to CP", "", 10, top=True)
    d.hl(None)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="demo_video")
    ap.add_argument("--port", type=int, default=8010)
    ap.add_argument("--no-llm", action="store_true")
    ap.add_argument("--scenario", default="ambush")
    args = ap.parse_args()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    base = f"http://127.0.0.1:{args.port}"

    env = dict(os.environ, C2_SOURCE="sim", C2_SIM_SPEED="1.5", C2_EXAMPLE_ORIGIN=f"{ORIGIN[0]},{ORIGIN[1]}")
    if args.no_llm:
        env["C2_LLM_ENABLED"] = "0"
    log = open(out / "backend.log", "w", encoding="utf-8")
    srv = subprocess.Popen([sys.executable, "-m", "uvicorn", "app.main:app", "--host", "127.0.0.1",
                            "--port", str(args.port)], env=env, stdout=log, stderr=subprocess.STDOUT)
    stop = threading.Event()
    try:
        for _ in range(60):
            try:
                http(base, "/api/health")
                break
            except Exception:
                time.sleep(0.5)
        threading.Thread(target=gps_feeder, args=(base, stop), daemon=True).start()
        time.sleep(6)  # let the simulated squad check in and the phones report
        with sync_playwright() as pw:
            browser = pw.chromium.launch()
            ctx = browser.new_context(viewport={"width": W, "height": H}, device_scale_factor=1,
                                      record_video_dir=str(out), record_video_size={"width": W, "height": H})
            page = ctx.new_page()
            d = Director(page)
            cop_scenes(d, base)
            example_scenes(d, base, args.scenario)
            d.step("")
            d.card("SQUAD C2", ["Sensor nodes decide on-board · gateway relays 18-byte packets · offline COP",
                                "Local sLLM operator: grounded brief + catalog actions, validated before display",
                                '<span class="acc">D4D × EDTH · Track 4 Tactical Edge</span>'], 6)
            video = page.video
            ctx.close()
            browser.close()
            path = Path(video.path())
            final = out / "squad_c2_demo.webm"
            if final.exists():
                final.unlink()
            path.rename(final)
            print("video", final, final.stat().st_size // 1024, "KB")
    finally:
        stop.set()
        srv.terminate()
        log.close()


if __name__ == "__main__":
    main()
