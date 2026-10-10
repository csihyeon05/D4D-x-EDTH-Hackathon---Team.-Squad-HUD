"""Example run: public-dataset personas -> incident -> operator brief -> solution -> outcome.

Eight soldiers S1..S8 are driven by recorded wristband traces of eight real
subjects (PhysioNet wearable-device-dataset 1.0.1, see data/personas.json).
Each second every soldier produces a sample; the same node rules as the
firmware decide on alerts, the packet is encoded to the 18-byte radio format
and decoded again, and a separate Engine (not the live COP engine) builds the
picture. The operator model then briefs and picks actions from the fixed
catalog; executing them changes what the soldiers do and the run reports the
outcome.

What is measured and what is modelled is listed in PROVENANCE and returned
with every state, so the page can label it.
"""
from __future__ import annotations

import asyncio
import json
import math
import os
import random
import statistics
import time
from dataclasses import dataclass, field
from pathlib import Path

from . import config
from .analysis.assess import sector_of
from .engine import Engine
from .node_rules import NodeRuleState, Sample, evaluate
from .protocol import AlertCode, Packet, PacketType, decode, encode
from .recommend import CATALOG

DATA = Path(os.environ.get("C2_EXAMPLE_PERSONAS") or Path(__file__).resolve().parent / "data" / "personas.json")
DEFAULT_ORIGIN = (37.8932, 127.0566)  # AO centre if no phone fix / C2_EXAMPLE_ORIGIN (open field, Gyeonggi-do)
CP = (150.0, 15.0)
RALLY_Y = 45.0
WALK_MPS = 1.4
PATROL_S = 50
SOLUTION_S = 75
BLEND_S = 8
PATROL_START = 600  # s into the aerobic session: warmed up, skin temperature settled
RUN_LIMIT_S = 600

SCENARIOS = {
    "ambush": {
        "title": "Contact in the front sector",
        "summary": "The lead fire team (S1-S4) goes to ground under fire; S3 takes an impact and stays down.",
        "expect": "POSSIBLE_CONTACT squad event, HR_SPIKE_IMMOBILE x3, FALL_IMMOBILE (S3)",
    },
    "casualty": {
        "title": "Single casualty",
        "summary": "S6 falls hard while crossing broken ground and does not get up.",
        "expect": "FALL_IMMOBILE (S6)",
    },
    "heat": {
        "title": "Heat strain on the march",
        "summary": "Two soldiers (S2, S7) keep a high workload in heat; skin temperature and heart rate climb.",
        "expect": "HEAT_STRAIN (S2, S7)",
    },
}

PROVENANCE = {
    "measured": [
        "Heart rate: Empatica E4 HR.csv of each subject (1 Hz)",
        "Skin temperature: E4 TEMP.csv (resampled to 1 Hz)",
        "Stillness motion while down: E4 accelerometer during the seated stress session",
        "Resting HR baseline: lowest 2-min mean HR in the seated stress session",
        "HR while down: the subject's own highest-HR stretch (aerobic or sprint session), from 45 s before the peak",
        "Recovery after an action: the same subject's HR decline after that peak",
    ],
    "modelled": [
        "Walking motion during patrol and movement (activity 35-70, wrist data was recorded on a bicycle ergometer)",
        "Positions and the GPS track (synthetic, around the AO centre)",
        "Impact peak 3.2 g at the moment of a fall (dataset has no falls)",
        "Skin-temperature rise in the heat scenario (+0.05 degC/s, max +2.4 degC)",
        "HR source changes between sessions are blended over 8 s",
        "Accelerometer bias removed per subject (10th percentile of the seated session)",
        "Skin-temperature sensor glitches outside 20-42 degC replaced by the last valid value",
        "Skin-temperature fall while resting (-0.05 degC/s of the added overlay)",
        "A soldier attending a casualty acknowledges that casualty's repeat alerts by radio",
    ],
}


def load_personas() -> dict | None:
    try:
        data = json.loads(DATA.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    for p in data.get("personas", []):
        prepare(p)
    return data


def _roll_extreme(xs: list[int], win: int, best) -> tuple[int, float]:
    """(start, mean) of the window whose sum is best (min or max)."""
    if len(xs) <= win:
        return 0, sum(xs) / max(1, len(xs))
    s = sum(xs[:win])
    b, bi = s, 0
    for i in range(1, len(xs) - win + 1):
        s += xs[i + win - 1] - xs[i - 1]
        if best(s, b) == s and s != b:
            b, bi = s, i
    return bi, b / win


def prepare(p: dict) -> None:
    """Derived values used by the run (computed at load, so an older personas.json works too)."""
    for sess in p["sessions"].values():
        # E4 skin-temperature glitches (e.g. 220 degC): hold the last plausible value
        last = next((t for t in sess["temp"] if 200 <= t <= 420), 330)
        clean = []
        for t in sess["temp"]:
            last = t if 200 <= t <= 420 else last
            clean.append(last)
        sess["temp"] = clean
    stress = p["sessions"]["stress"]
    # resting HR = lowest 2-min mean HR of the seated session
    p["resting_hr"] = round(_roll_extreme(stress["hr"], 120, min)[1])
    # accelerometer bias when still (10th percentile of the seated session), removed like a node's power-on calibration
    act = sorted(stress["act"])
    p["act_bias"] = act[len(act) // 10] if act else 0
    # session with the highest HR, used for "under fire / exertion" and the recovery after it
    p["peak_session"] = max(("aerobic", "anaerobic"), key=lambda k: max(p["sessions"][k]["hr"]))


# ----------------------------------------------------------------------------- traces
@dataclass
class Trace:
    """Cursor into one recorded session; holds the last value at the end."""
    sess: dict
    i: int = 0

    def at(self, key: str, k: int | None = None) -> float:
        xs = self.sess[key]
        return float(xs[min(max(0, self.i if k is None else k), len(xs) - 1)])

    def step(self) -> None:
        self.i += 1


def _peak_index(xs: list[int]) -> int:
    return max(range(len(xs)), key=lambda k: xs[k])


def _stillest(act: list[int], win: int = 120) -> int:
    """Start of the window with the lowest mean activity (real stillness)."""
    return _roll_extreme(act, win, min)[0]


def _high_block(hr: list[int], win: int = 180) -> int:
    """Start of the window with the highest mean HR (hardest sustained work)."""
    return _roll_extreme(hr, win, max)[0]


# ----------------------------------------------------------------------------- soldiers
@dataclass
class ExSoldier:
    sid: int
    persona: dict
    x: float
    y: float
    offset: tuple[float, float]
    rules: NodeRuleState
    hr_trace: Trace
    temp_trace: Trace
    still_trace: Trace | None = None
    mode: str = "patrol"          # patrol | down | heat | rest | aid | carried | carrier | move | hold
    hr_prev: float | None = None  # for blending when the HR source changes
    blend_left: int = 0
    impact_at: float | None = None
    temp_overlay: float = 0.0
    overlay_rate: float = 0.0
    goal: tuple[float, float] | None = None
    follow: int | None = None     # soldier id to stay next to (carrier / carried)
    picked: bool = False          # casualty has been reached by the carrier
    attended_by: int | None = None  # a soldier is with this casualty and confirms status by radio
    aid_for: int | None = None    # helper: casualty this soldier is moving to
    seq: int = 0
    heading: float = 0.0
    track: list = field(default_factory=list)
    hr_hist: list = field(default_factory=list)
    temp_hist: list = field(default_factory=list)
    alerts: int = 0

    @property
    def callsign(self) -> str:
        return f"S{self.sid}"

    def switch_hr(self, sess: dict, start: int) -> None:
        self.hr_prev = self.hr_trace.at("hr")
        self.hr_trace = Trace(sess, start)
        self.blend_left = BLEND_S


class ExampleSim:
    def __init__(self, origin_provider=None) -> None:
        self.data = load_personas()
        self.origin_provider = origin_provider
        self.task: asyncio.Task | None = None
        self.reset()

    # ------------------------------------------------------------------ lifecycle
    def reset(self) -> None:
        if self.task:
            self.task.cancel()
            self.task = None
        self.engine = Engine()
        self.phase = "ready"
        self.scenario: str | None = None
        self.t = 0.0
        self.speed = 2.0
        self.soldiers: list[ExSoldier] = []
        self.timeline: list[dict] = []
        self.brief: dict | None = None
        self.executed: list[dict] = []
        self.incident_t: float | None = None
        self.first_alert_t: float | None = None
        self.brief_t: float | None = None
        self.solution_t: float | None = None
        self.before: dict | None = None
        self.outcome: dict | None = None
        self.hostile_sector: str | None = None
        self.origin = self._origin()
        self.rng = random.Random(11)

    def _origin(self) -> tuple[float, float]:
        if config.EXAMPLE_ORIGIN:
            try:
                lat, lon = (float(v) for v in config.EXAMPLE_ORIGIN.split(","))
                return lat, lon
            except ValueError:
                pass
        if self.origin_provider:
            o = self.origin_provider()
            if o:
                return o
        return DEFAULT_ORIGIN

    def personas_info(self) -> dict:
        if not self.data:
            return {"ok": False, "error": "personas.json missing: run tools/personas/build_personas.py once with internet"}
        rows = []
        for i, p in enumerate(self.data["personas"][:8]):
            s = p["sessions"]
            rows.append({
                "callsign": f"S{i + 1}", "subject": p["subject"], "sex": p["sex"], "age": p["age"],
                "height_cm": p["height_cm"], "weight_kg": p["weight_kg"],
                "regular_activity": p["regular_activity"], "resting_hr": p["resting_hr"],
                "resting_temp_c": p["resting_temp_c"],
                "max_hr": max(max(s[k]["hr"]) for k in s),
                "session_s": {k: len(v["hr"]) for k, v in s.items()},
            })
        return {"ok": True, "source": self.data["source"], "personas": rows,
                "scenarios": SCENARIOS, "provenance": PROVENANCE}

    def start(self, scenario: str, speed: float = 2.0, autorun: bool = True) -> dict:
        if not self.data:
            return {"ok": False, "error": "personas.json missing"}
        if scenario not in SCENARIOS:
            return {"ok": False, "error": f"unknown scenario {scenario}", "available": list(SCENARIOS)}
        self.reset()
        self.scenario, self.speed, self.phase = scenario, max(0.5, min(10.0, speed)), "patrol"
        for i, p in enumerate(self.data["personas"][:8]):
            aer = p["sessions"]["aerobic"]
            off = ((i % 4) * 18.0 - 27.0, -(i // 4) * 16.0)
            start = min(PATROL_START, len(aer["hr"]) - 1)
            temp_trace = Trace(aer, start)
            # skin-temperature baseline: median of the next 5 min of the same trace (wrist has warmed up)
            temp0 = statistics.median(aer["temp"][start:start + 300] or aer["temp"]) / 10
            self.soldiers.append(ExSoldier(
                sid=i + 1, persona=p, x=CP[0] + off[0], y=40 + off[1], offset=off,
                rules=NodeRuleState(float(p["resting_hr"]), temp0),
                hr_trace=Trace(aer, start), temp_trace=temp_trace,
            ))
        self.log("start", f"Scenario '{SCENARIOS[scenario]['title']}' with {len(self.soldiers)} personas "
                          f"({', '.join(s.persona['subject'] for s in self.soldiers)}).")
        if autorun:
            self.task = asyncio.get_running_loop().create_task(self._run())
        return {"ok": True, "scenario": scenario, "speed": self.speed}

    async def _run(self) -> None:
        try:
            while self.phase not in ("outcome",) and self.t < RUN_LIMIT_S:
                self.step()
                await asyncio.sleep(1.0 / self.speed)
            # a few seconds more so the last packets land
            for _ in range(3):
                self.step()
                await asyncio.sleep(1.0 / self.speed)
        except asyncio.CancelledError:
            pass

    def log(self, kind: str, text: str) -> None:
        self.timeline.append({"t_s": round(self.t), "kind": kind, "text": text})

    # ------------------------------------------------------------------ scenario events
    def _trigger_incident(self) -> None:
        self.phase, self.incident_t = "incident", self.t
        by = {s.sid: s for s in self.soldiers}
        if self.scenario == "ambush":
            group = [by[i] for i in (1, 2, 3, 4)]
            for s in group:
                self._go_down(s, impact=s.sid == 3)
            self.log("incident", "S1-S4 go to ground; S3 impact (modelled 3.2 g). Others halt.")
            for s in self.soldiers:
                if s.mode == "patrol":
                    s.mode = "hold"
        elif self.scenario == "casualty":
            self._go_down(by[6], impact=True)
            self.log("incident", "S6 falls (modelled 3.2 g impact) and stays down.")
        elif self.scenario == "heat":
            for s in (by[2], by[7]):
                aer = s.persona["sessions"]["aerobic"]
                s.switch_hr(aer, _high_block(aer["hr"]))
                s.mode, s.overlay_rate = "heat", 0.05
            self.log("incident", "S2, S7 at peak workload (measured HR); skin-temperature rise modelled.")

    def _go_down(self, s: ExSoldier, impact: bool) -> None:
        p = s.persona["sessions"]
        top = p[s.persona["peak_session"]]
        pk = _peak_index(top["hr"])
        s.switch_hr(top, max(0, pk - 45))
        s.still_trace = Trace(p["stress"], _stillest(p["stress"]["act"]))
        s.mode = "down"
        s.impact_at = self.t if impact else None

    # ------------------------------------------------------------------ solution
    def apply(self, actions: list[dict]) -> dict:
        if self.phase not in ("incident", "brief"):
            return {"ok": False, "error": f"cannot execute in phase {self.phase}"}
        if not actions:
            return {"ok": False, "error": "no action selected"}
        self.before = self._metrics()
        by = {s.callsign: s for s in self.soldiers}
        done = []
        for a in actions:
            act = str(a.get("action", "")).upper()
            if act not in CATALOG:
                continue
            targets = [by[c] for c in a.get("targets") or [] if c in by]
            sector = a.get("sector")
            if act == "MEDEVAC":
                for cas in targets:
                    carrier = self._nearest_helper(cas)
                    cas.mode, cas.goal = "carried", CP
                    self._recover_hr(cas)
                    if carrier:
                        carrier.mode, carrier.follow, carrier.goal = "carrier", cas.sid, None
                        cas.follow = carrier.sid
                    self.engine.acknowledge(cas.sid)
            elif act == "BUDDY_AID":
                for cas in targets:
                    helper = self._nearest_helper(cas)
                    if cas.mode == "down":
                        cas.mode = "aid"
                    self._recover_hr(cas)
                    if helper:
                        helper.mode, helper.goal, helper.aid_for = "move", (cas.x + 2, cas.y - 2), cas.sid
                    self.engine.acknowledge(cas.sid)
            elif act == "REST_AND_COOL":
                for s in targets:
                    s.mode, s.overlay_rate = "rest", -0.05
                    self._recover_hr(s)
                    self.engine.acknowledge(s.sid)
            elif act == "WITHDRAW_FROM_SECTOR" and sector:
                for s in self.soldiers:
                    if sector_of(s.x, s.y) == sector and s.mode not in ("carried", "carrier"):
                        if s.mode in ("down", "aid") and s.impact_at is not None:
                            continue  # casualties move only with MEDEVAC
                        self._set_moving(s, (s.x, RALLY_Y + s.offset[1] * 0.5))
                        self.engine.acknowledge(s.sid)
            elif act == "REGROUP_AT_CP":
                for s in self.soldiers:
                    if s.mode not in ("carried", "carrier") and not (s.mode == "down" and s.impact_at is not None):
                        self._set_moving(s, (CP[0] + s.offset[0] * 0.5, CP[1] + 12 + s.offset[1] * 0.3))
                        self.engine.acknowledge(s.sid)
            # HOLD_AND_OBSERVE: nothing changes on purpose
            done.append({"action": act, "label": CATALOG[act]["label"], "targets": [t.callsign for t in targets],
                         "sector": sector, "source": a.get("source")})
        if not done:
            return {"ok": False, "error": "no valid action"}
        self.executed = done
        self.solution_t, self.phase = self.t, "solution"
        self.log("solution", "Executed: " + "; ".join(
            d["label"] + (f" {', '.join(d['targets'])}" if d["targets"] else "") + (f" {d['sector']}" if d["sector"] else "")
            for d in done))
        return {"ok": True, "executed": done}

    def _nearest_helper(self, cas: ExSoldier) -> ExSoldier | None:
        ok = [s for s in self.soldiers if s is not cas and s.mode in ("patrol", "hold", "move")]
        return min(ok, key=lambda s: math.dist((s.x, s.y), (cas.x, cas.y)), default=None)

    def _recover_hr(self, s: ExSoldier) -> None:
        """Continue on the subject's measured recovery: HR after their highest-HR peak."""
        sess = s.persona["sessions"][s.persona["peak_session"]]
        pk = _peak_index(sess["hr"])
        start = max(s.hr_trace.i, pk + 5) if s.hr_trace.sess is sess else pk + 5
        s.switch_hr(sess, start)

    def _set_moving(self, s: ExSoldier, goal: tuple[float, float]) -> None:
        aer = s.persona["sessions"]["aerobic"]
        if s.mode in ("down", "heat", "rest"):
            s.switch_hr(aer, min(PATROL_START, len(aer["hr"]) - 1))
        s.mode, s.goal = "move", goal

    # ------------------------------------------------------------------ tick
    def step(self) -> None:
        self.t += 1
        if self.phase == "patrol" and self.t >= PATROL_S:
            self._trigger_incident()
        moving_lead = self.phase == "patrol"
        lead_y = 40 + WALK_MPS * min(self.t, PATROL_S)
        by = {s.sid: s for s in self.soldiers}
        for s in self.soldiers:
            sample = self._sample(s, moving_lead, lead_y, by)
            code = evaluate(s.rules, sample)
            ptype = PacketType.ALERT if code is not None else PacketType.CHECKIN
            s.seq += 1
            pkt = Packet(ptype=ptype, soldier_id=s.sid, seq=s.seq, t_s=int(self.t),
                         code=int(code or AlertCode.NONE), hr_bpm=round(sample.hr_bpm),
                         skin_temp_c=sample.skin_temp_c, activity=round(sample.activity),
                         x_m=s.x, y_m=s.y, gas=round(sample.gas), hops=0)
            raw = encode(pkt)
            rssi = round(-45 - 20 * math.log10(max(1.0, math.dist((s.x, s.y), CP))))
            events = self.engine.ingest(decode(raw, rssi), self.t, len(raw))
            if s.attended_by is not None and any(e["type"] == "alert" for e in events):
                # the soldier with the casualty confirms status by radio (modelled acknowledgement)
                self.engine.acknowledge(s.sid)
                self.log("solution", f"S{s.attended_by} confirms {s.callsign} is being treated (acknowledged).")
            for e in events:
                if e["type"] == "alert":
                    s.alerts += 1
                    self.first_alert_t = self.first_alert_t or self.t
                    self.log("alert", f"{e['callsign']}: {e['label']} (HR {e['hr_bpm']}, {e['sector']})")
                else:
                    if e["kind"] == "POSSIBLE_CONTACT" and self.hostile_sector is None:
                        self.hostile_sector = e["sector"]
                    self.log("squad", f"{e['kind']} in {e['sector']}: {', '.join(f'S{i}' for i in e['soldier_ids'])}")
            s.hr_hist.append(round(sample.hr_bpm))
            s.temp_hist.append(round(sample.skin_temp_c, 2))
            del s.hr_hist[:-180], s.temp_hist[:-180]
        if self.phase == "solution" and self.t - self.solution_t >= SOLUTION_S:
            self.outcome = self._outcome()
            self.phase = "outcome"
            self.log("outcome", self.outcome["summary"])

    def _sample(self, s: ExSoldier, moving_lead: bool, lead_y: float, by: dict) -> Sample:
        r = self.rng
        # heart rate from the recorded trace, blended after a source switch
        hr = s.hr_trace.at("hr")
        if s.blend_left > 0 and s.hr_prev is not None:
            w = s.blend_left / (BLEND_S + 1)
            hr = w * s.hr_prev + (1 - w) * hr
            s.blend_left -= 1
        s.hr_trace.step()
        # skin temperature: recorded patrol session + labelled overlay
        s.temp_overlay = max(0.0, min(2.4, s.temp_overlay + s.overlay_rate))
        temp = s.temp_trace.at("temp") / 10 + s.temp_overlay
        s.temp_trace.step()

        accel, walking = 1.0, False
        if s.mode == "patrol":
            if moving_lead:
                self._walk_to(s, (CP[0] + s.offset[0], lead_y + s.offset[1]))
                walking = True
        elif s.mode in ("move", "carrier", "carried"):
            if s.mode == "carrier" and s.follow is not None:
                cas = by[s.follow]
                if not cas.picked and math.dist((s.x, s.y), (cas.x, cas.y)) > 2.5:
                    walking = self._walk_to(s, (cas.x, cas.y - 1))
                else:
                    if not cas.picked:
                        cas.picked, cas.attended_by = True, s.sid
                        self.engine.acknowledge(cas.sid)
                        self.log("solution", f"{s.callsign} reached {cas.callsign}; carrying to CP.")
                    walking = self._walk_to(s, (CP[0] + 3, CP[1] + 3), speed=0.9)
            elif s.mode == "carried":
                if s.picked:
                    carrier = by.get(s.follow) if s.follow else None
                    if carrier:
                        s.x, s.y = carrier.x - 1.0, carrier.y
                    walking = False
            elif s.goal:
                walking = self._walk_to(s, s.goal)
                if not walking and s.mode == "move" and s.aid_for is not None:
                    cas = by[s.aid_for]
                    cas.attended_by, s.aid_for = s.sid, None
                    self.engine.acknowledge(cas.sid)
                    self.log("solution", f"{s.callsign} reached {cas.callsign}; giving first aid.")
                if not walking and s.mode == "move":
                    s.mode = "hold"
        if s.mode in ("down", "aid") or (s.mode == "carried" and not s.picked):
            st = s.still_trace
            act = max(0.0, st.at("act") - s.persona["act_bias"]) if st else r.uniform(0, 4)
            accel = (st.at("peak") / 10) if st else 1.0
            if st:
                st.step()
            if s.impact_at is not None and self.t - s.impact_at < 1.5:
                accel = 3.2
        elif s.mode == "carried":
            act, accel = r.uniform(20, 40), 1.0 + r.uniform(0.05, 0.2)
        elif walking or s.mode == "heat":
            act = r.uniform(35, 70)  # walking, or heavy work in place (heat)
            accel = 1.0 + act / 255 + r.uniform(0, 0.15)
        else:  # hold / rest: standing or sitting, small movements (modelled)
            act = r.uniform(9, 20)
            accel = 1.0 + r.uniform(0, 0.05)
        return Sample(t_s=self.t, hr_bpm=hr, skin_temp_c=temp, accel_g=accel, activity=act,
                      gas=r.uniform(5, 20))

    def _walk_to(self, s: ExSoldier, goal: tuple[float, float], speed: float = WALK_MPS) -> bool:
        dx, dy = goal[0] - s.x, goal[1] - s.y
        d = math.hypot(dx, dy)
        if d < 0.3:
            return False
        k = min(1.0, speed / d)
        s.x += dx * k
        s.y += dy * k
        s.heading = (math.degrees(math.atan2(dx, dy)) + 360) % 360
        s.track.append(self._latlon(s.x, s.y))
        del s.track[:-40]
        return True

    # ------------------------------------------------------------------ metrics
    def _metrics(self) -> dict:
        snap = self.engine.snapshot(self.t)
        counts = snap["counts"]
        flagged = [x for x in snap["soldiers"] if x["severity"] in ("CRITICAL", "WARN")]
        hrs = [x["hr_bpm"] for x in flagged if x["hr_bpm"]]
        in_hostile = sum(1 for s in self.soldiers if self.hostile_sector and sector_of(s.x, s.y) == self.hostile_sector)
        cas = [s for s in self.soldiers if s.impact_at is not None]
        return {
            "t_s": round(self.t),
            "critical": counts.get("CRITICAL", 0), "warn": counts.get("WARN", 0),
            "flagged": [x["callsign"] for x in flagged],
            "mean_hr_flagged": round(statistics.fmean(hrs)) if hrs else None,
            "in_hostile_sector": in_hostile if self.hostile_sector else None,
            "casualty_to_cp_m": round(min(math.dist((c.x, c.y), CP) for c in cas)) if cas else None,
        }

    def _outcome(self) -> dict:
        after = self._metrics()
        b = self.before or after
        affected = sorted({t for d in self.executed for t in d["targets"]})
        hr_change = []
        for s in self.soldiers:
            if s.callsign in affected and len(s.hr_hist) > SOLUTION_S:
                hr_change.append({"callsign": s.callsign, "at_execute": s.hr_hist[-SOLUTION_S - 1],
                                  "now": s.hr_hist[-1], "rest": s.persona["resting_hr"]})
        parts = [f"Critical {b['critical']} -> {after['critical']}", f"warn {b['warn']} -> {after['warn']}"]
        if b.get("in_hostile_sector") is not None:
            parts.append(f"in contact sector {b['in_hostile_sector']} -> {after['in_hostile_sector']}")
        if b.get("casualty_to_cp_m") is not None:
            parts.append(f"casualty to CP {b['casualty_to_cp_m']} m -> {after['casualty_to_cp_m']} m")
        return {
            "before": b, "after": after, "hr_change": hr_change,
            "time_to_first_alert_s": None if self.first_alert_t is None or self.incident_t is None
            else round(self.first_alert_t - self.incident_t),
            "time_alert_to_action_s": None if self.first_alert_t is None or self.solution_t is None
            else round(self.solution_t - self.first_alert_t),
            "summary": ", ".join(parts) + ".",
        }

    # ------------------------------------------------------------------ views
    def _latlon(self, x: float, y: float) -> tuple[float, float]:
        lat0, lon0 = self.origin
        dn, de = y - 150.0, x - 150.0
        return (round(lat0 + dn / 110_540, 7), round(lon0 + de / (111_320 * math.cos(math.radians(lat0))), 7))

    def facts(self) -> dict:
        f = self.engine.facts(self.t)
        f["squad_callsigns"] = [s.callsign for s in self.soldiers]
        f["commander_post"] = sector_of(*CP)
        return f

    def state(self) -> dict:
        snap = self.engine.snapshot(self.t)
        now_ms = int(time.time() * 1000)
        by = {s.sid: s for s in self.soldiers}
        for sd in snap["soldiers"]:
            s = by.get(sd["soldier_id"])
            if not s:
                continue
            lat, lon = self._latlon(s.x, s.y)
            sd["gps"] = {
                "device_id": f"example-{s.callsign}", "device_name": "synthetic", "soldier_id": s.sid,
                "lat": lat, "lon": lon, "accuracy_m": 3.0, "alt_m": None, "speed_mps": None,
                "fix_unix_ms": now_ms, "recv_unix_ms": now_ms, "heading_deg": round(s.heading),
                "heading_source": "gps", "id_conflict": False, "age_s": 0, "track": s.track[-30:],
            }
            sd["subject"] = s.persona["subject"]
            sd["mode"] = s.mode
            sd["resting_hr"] = s.persona["resting_hr"]
            sd["hr_hist"] = s.hr_hist[-120:]
            sd["temp_overlay_c"] = round(s.temp_overlay, 2)
        return {
            "phase": self.phase, "scenario": self.scenario,
            "scenario_info": SCENARIOS.get(self.scenario) if self.scenario else None,
            "t_s": self.t, "speed": self.speed,
            "soldiers": snap["soldiers"], "alerts": snap["alerts"][:30], "squad_events": snap["squad_events"],
            "counts": snap["counts"], "stats": snap["stats"],
            "timeline": self.timeline[-60:], "brief": self.brief, "executed": self.executed,
            "outcome": self.outcome, "hostile_sector": self.hostile_sector,
            "origin": {"lat": self.origin[0], "lon": self.origin[1]},
            "cp": {"x_m": CP[0], "y_m": CP[1], "sector": sector_of(*CP)},
            "solution_progress": None if self.solution_t is None
            else min(1.0, (self.t - self.solution_t) / SOLUTION_S),
        }
