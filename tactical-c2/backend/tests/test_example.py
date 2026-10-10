"""Example run: personas -> incident -> alerts -> recommendations -> solution -> outcome."""
import asyncio
import json
import math
import random

import pytest

from app import example
from app.briefing import Briefer
from app.recommend import rule_recommendations, validate


def _session(n, base, amp, peak_at=None, still=False, rng=None):
    hr, temp, act, peak = [], [], [], []
    for i in range(n):
        h = base + amp * math.exp(-((i - peak_at) / 60) ** 2) if peak_at is not None else base + amp * min(1, i / 600)
        hr.append(round(h + rng.uniform(-2, 2)))
        temp.append(round((33.0 + 0.3 * min(1, i / 900)) * 10))
        a = rng.uniform(1, 5) if still else rng.uniform(10, 60)
        act.append(round(a))
        peak.append(round((1.0 + a / 255) * 10))
    return {"start_utc": "2024-01-01T00:00:00Z", "tags": [], "hr": hr, "temp": temp, "act": act, "peak": peak}


@pytest.fixture()
def personas(tmp_path, monkeypatch):
    rng = random.Random(3)
    rows = []
    for k in range(8):
        rest = 65 + k
        rows.append({"subject": f"S{k + 1:02d}", "sex": "m", "age": 25, "height_cm": 175, "weight_kg": 70,
                     "regular_activity": True, "resting_hr": rest, "resting_temp_c": 33.0,
                     "sessions": {"stress": _session(1500, rest, 5, still=True, rng=rng),
                                  "aerobic": _session(1800, rest + 30, 70, rng=rng),
                                  "anaerobic": _session(900, rest + 20, 95, peak_at=400, rng=rng)}})
    path = tmp_path / "personas.json"
    path.write_text(json.dumps({"source": {"title": "test"}, "personas": rows}))
    monkeypatch.setattr(example, "DATA", path)
    return path


def _run(sim, seconds):
    for _ in range(seconds):
        sim.step()


def test_ambush_flow(personas):
    sim = example.ExampleSim()
    assert sim.start("ambush", autorun=False)["ok"]
    _run(sim, example.PATROL_S + 40)
    st = sim.state()
    assert st["phase"] == "incident"
    labels = {(a["callsign"], a["label"]) for a in st["alerts"]}
    assert ("S3", "Fall, then no movement") in labels
    assert any(e["kind"] == "POSSIBLE_CONTACT" for e in st["squad_events"])
    assert all(s["gps"] and abs(s["gps"]["lat"] - sim.origin[0]) < 0.01 for s in st["soldiers"])

    recs = rule_recommendations(sim.facts())
    actions = {r["action"] for r in recs}
    assert {"MEDEVAC", "WITHDRAW_FROM_SECTOR"} <= actions
    assert sim.apply(recs)["ok"]
    _run(sim, example.SOLUTION_S + 1)
    out = sim.state()
    assert out["phase"] == "outcome"
    o = out["outcome"]
    assert o["after"]["critical"] < o["before"]["critical"]
    assert o["after"]["in_hostile_sector"] < o["before"]["in_hostile_sector"]
    assert o["after"]["casualty_to_cp_m"] < o["before"]["casualty_to_cp_m"]


def test_casualty_and_heat(personas):
    sim = example.ExampleSim()
    sim.start("casualty", autorun=False)
    _run(sim, example.PATROL_S + 30)
    assert any(a["callsign"] == "S6" and a["code"] == 3 for a in sim.state()["alerts"])

    sim = example.ExampleSim()
    sim.start("heat", autorun=False)
    _run(sim, example.PATROL_S + 90)
    heat = {a["callsign"] for a in sim.state()["alerts"] if a["code"] == 4}
    assert heat == {"S2", "S7"}
    recs = rule_recommendations(sim.facts())
    assert recs[0]["action"] == "REST_AND_COOL" and set(recs[0]["targets"]) == {"S2", "S7"}
    sim.apply(recs)
    _run(sim, example.SOLUTION_S + 1)
    assert sim.state()["outcome"]["after"]["warn"] == 0


def test_validate_rejects_hallucinations(personas):
    sim = example.ExampleSim()
    sim.start("ambush", autorun=False)
    _run(sim, example.PATROL_S + 40)
    f = sim.facts()
    valid, rejected = validate([
        {"action": "MEDEVAC", "targets": ["S3"], "reason": "S3 fall"},
        {"action": "CALL_AIRSTRIKE", "sector": "B2"},
        {"action": "MEDEVAC", "targets": ["S9"]},
        {"action": "WITHDRAW_FROM_SECTOR", "sector": "Z9"},
        {"action": "BUDDY_AID", "targets": ["S3"], "reason": "S12 is closest"},
    ], f)
    assert [v["action"] for v in valid] == ["MEDEVAC", "BUDDY_AID"]
    assert valid[1]["reason"] == ""
    assert len(rejected) == 3


def test_recommend_falls_back_without_llm(personas, monkeypatch):
    from app import config
    monkeypatch.setattr(config, "LLM_ENABLED", False)
    sim = example.ExampleSim()
    sim.start("casualty", autorun=False)
    _run(sim, example.PATROL_S + 30)
    b = asyncio.run(Briefer().recommend(sim.facts()))
    assert b["recommendation_source"] == "rules" and b["recommendations"][0]["action"] == "MEDEVAC"
    assert "S6" in b["text"]


def test_recommend_uses_llm_json(personas, monkeypatch):
    from app import config, llm
    monkeypatch.setattr(config, "LLM_ENABLED", True)

    async def fake(system, user, **kw):
        assert "ACTIONS" in user and "MEDEVAC" in user
        return {"brief": "Critical: S6 suspected hit or fall, not moving.",
                "recommendations": [{"action": "BUDDY_AID", "targets": ["S6"], "reason": "S6 is down."},
                                    {"action": "NUKE", "targets": []}]}
    monkeypatch.setattr(llm, "generate_json", fake)
    sim = example.ExampleSim()
    sim.start("casualty", autorun=False)
    _run(sim, example.PATROL_S + 30)
    b = asyncio.run(Briefer().recommend(sim.facts()))
    assert b["source"] == "llm" and b["recommendation_source"] == "llm+rules"
    assert [(r["action"], r["source"]) for r in b["recommendations"]] == [("BUDDY_AID", "llm"), ("MEDEVAC", "rules")]
    assert b["rejected"]


def test_catalog_preconditions(personas):
    sim = example.ExampleSim()
    sim.start("heat", autorun=False)
    _run(sim, example.PATROL_S + 90)
    valid, rejected = validate([
        {"action": "MEDEVAC", "targets": ["S2"]},           # heat strain is not a casualty
        {"action": "WITHDRAW_FROM_SECTOR", "sector": "B2"},  # no squad event
        {"action": "HOLD_AND_OBSERVE"},                      # WARN soldiers exist
        {"action": "REST_AND_COOL", "targets": ["S2", "S7"]},
    ], sim.facts())
    assert [v["action"] for v in valid] == ["REST_AND_COOL"]
    assert len(rejected) == 3


def test_chatml_and_json_parse():
    from app.llm import chatml, parse_json_object
    p = chatml("sys", "usr")
    assert p.startswith("<|im_start|>system\nsys<|im_end|>") and p.endswith("<think>\n\n</think>\n\n")
    assert parse_json_object('```json\n{"a": 1}\n```') == {"a": 1}


def test_brief_guard_rejects_moved_conditions(personas):
    from app.briefing import _grounded
    sim = example.ExampleSim()
    sim.start("casualty", autorun=False)
    _run(sim, example.PATROL_S + 30)
    f = sim.facts()
    assert _grounded("Critical: S6 suspected hit or fall in B1.", f)
    assert not _grounded("S6 suspected hit or fall. S8: heat strain.", f)   # S8 has no alert
    assert not _grounded("Critical: S2 is down.", f)                       # S2 is not CRITICAL
    assert not _grounded("S42 is missing.", f)                             # unknown soldier
