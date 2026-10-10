import math

from app.analysis.assess import sector_of, strain_index
from app.analysis.pdr import PDR
from app.briefing import _grounded, template_briefing
from app.engine import Engine
from app.protocol import (PACKET_SIZE, AlertCode, Packet, PacketError, PacketType, decode, encode,
                          parse_gateway_line)
from app.simulator import SquadSimulator


def _pkt(**kw):
    base = dict(ptype=PacketType.ALERT, soldier_id=3, seq=7, t_s=125, code=AlertCode.HR_SPIKE_IMMOBILE,
                hr_bpm=141, skin_temp_c=34.56, activity=2, x_m=123.4, y_m=-5.6, gas=12, hops=1)
    base.update(kw)
    return Packet(**base)


def test_packet_roundtrip_and_size():
    raw = encode(_pkt())
    assert len(raw) == PACKET_SIZE == 18
    p = decode(raw, rssi=-61)
    assert (p.soldier_id, p.code, p.hr_bpm, p.x_m, p.y_m, p.rssi) == (3, 1, 141, 123.4, -5.6, -61)
    assert abs(p.skin_temp_c - 34.56) < 0.01


def test_crc_rejects_corruption():
    raw = bytearray(encode(_pkt()))
    raw[5] ^= 0xFF
    try:
        decode(bytes(raw))
        assert False, "corrupted packet accepted"
    except PacketError:
        pass


def test_gateway_line():
    p = parse_gateway_line("PKT:" + encode(_pkt()).hex() + ",RSSI:-70\r\n")
    assert p.rssi == -70 and p.soldier_id == 3
    assert parse_gateway_line("debug: boot ok") is None


def test_sector_grid():
    assert sector_of(10, 10) == "A1"
    assert sector_of(150, 150) == "B2"
    assert sector_of(299, 299) == "C3"
    assert sector_of(-5, 999) == "A3"  # clamped


def test_psi_bounds():
    assert strain_index(70, 33.5, 70, 33.5) == 0.0
    assert 0 < strain_index(150, 35.5, 70, 33.5) <= 10


def test_pdr_straight_walk():
    pdr = PDR(heading0_rad=0.0)
    t = 0.0
    for _ in range(10 * 50):  # 10 s at 50 Hz, ~2 steps/s
        a = 1.0 + 0.35 * math.sin(2 * math.pi * 2 * t)
        pdr.update(t, a, 0.0, 0.02)
        t += 0.02
    assert 18 <= pdr.s.steps <= 21
    assert abs(pdr.s.x) < 1e-6 and pdr.s.y > 10  # walked north


def test_squad_correlation_flags_contact():
    eng = Engine()
    for sid in (1, 2):
        eng.ingest(_pkt(soldier_id=sid, ptype=PacketType.CHECKIN, code=0, hr_bpm=70, x_m=150, y_m=150), 0, 18)
    ev = eng.ingest(_pkt(soldier_id=1, x_m=150, y_m=150), 10, 18)
    assert not any(e["type"] == "squad" for e in ev)
    ev = eng.ingest(_pkt(soldier_id=2, x_m=160, y_m=155), 15, 18)
    squad = [e for e in ev if e["type"] == "squad"]
    assert squad and squad[0]["kind"] == "POSSIBLE_CONTACT" and squad[0]["sector"] == "B2"
    # not re-raised inside the window
    ev = eng.ingest(_pkt(soldier_id=2, x_m=160, y_m=155), 20, 18)
    assert not any(e["type"] == "squad" for e in ev)


def test_correlation_across_sector_boundary():
    eng = Engine()
    eng.ingest(_pkt(soldier_id=1, x_m=195, y_m=150), 10, 18)   # B2
    ev = eng.ingest(_pkt(soldier_id=2, x_m=210, y_m=152), 12, 18)  # C2, 15 m away
    assert any(e["type"] == "squad" and e["kind"] == "POSSIBLE_CONTACT" for e in ev)
    # far-apart soldiers are not grouped
    eng2 = Engine()
    eng2.ingest(_pkt(soldier_id=1, x_m=20, y_m=20), 10, 18)
    ev = eng2.ingest(_pkt(soldier_id=2, x_m=280, y_m=280), 12, 18)
    assert not any(e["type"] == "squad" for e in ev)


def test_grounding_guard():
    facts = {"mission_time": "T+01:00", "squad_size": 8, "status_counts": {},
             "flagged_soldiers": [{"callsign": "S3", "severity": "CRITICAL", "active_label": "x",
                                   "interpretation": "y", "sector": "B2", "hr_bpm": 140,
                                   "skin_temp_c": 34, "psi_est": 3, "seconds_since_seen": 1}],
             "squad_events": []}
    assert _grounded("S3 in B2 suspected injury.", facts)
    assert not _grounded("S3 and S7 are down.", facts)
    assert "S3" in template_briefing(facts)


def _run(sim, eng, seconds):
    events = []
    for _ in range(seconds):
        for raw, rssi in sim.step(1.0):
            events += eng.ingest(decode(raw, rssi), sim.t, len(raw))
    return events


def test_sim_is_mostly_silent_and_baselines():
    sim, eng = SquadSimulator(n=8, seed=1), Engine()
    _run(sim, eng, 120)
    assert len(eng.soldiers) == 8
    assert all(t.hr0 is not None for t in eng.soldiers.values())
    assert sim.emission_stats()["radio_silence_pct"] > 95


def test_sim_scenarios_produce_expected_alerts():
    for name, code in [("injury", AlertCode.HR_SPIKE_IMMOBILE), ("unconscious", AlertCode.FALL_IMMOBILE),
                       ("heat", AlertCode.HEAT_STRAIN), ("sos", AlertCode.SITREP_SOS)]:
        sim, eng = SquadSimulator(n=8, seed=2), Engine()
        _run(sim, eng, 20)
        sid = sim.trigger(name)["soldiers"][0]
        ev = _run(sim, eng, 240 if name == "heat" else 40)
        codes = {e["code"] for e in ev if e["type"] == "alert" and e["soldier_id"] == sid}
        assert code in codes, (name, codes)


def test_sim_ambush_and_hazard_correlate():
    sim, eng = SquadSimulator(n=8, seed=3), Engine()
    _run(sim, eng, 20)
    sim.trigger("ambush")
    kinds = {e["kind"] for e in _run(sim, eng, 40) if e["type"] == "squad"}
    assert "POSSIBLE_CONTACT" in kinds

    sim, eng = SquadSimulator(n=8, seed=3), Engine()
    _run(sim, eng, 20)
    sim.trigger("hazard")
    kinds = {e["kind"] for e in _run(sim, eng, 20) if e["type"] == "squad"}
    assert "HAZARD_AREA" in kinds


def test_missing_vitals_remain_unknown_and_do_not_poison_baseline():
    engine = Engine()
    missing = decode(encode(_pkt(ptype=PacketType.CHECKIN, code=AlertCode.NONE, hr_bpm=None, skin_temp_c=None)))
    assert missing.hr_bpm is None and missing.skin_temp_c is None
    engine.ingest(missing, 0, PACKET_SIZE)
    track = engine.snapshot(0)["soldiers"][0]
    assert track["hr_bpm"] is None and track["skin_temp_c"] is None
    assert track["baseline"] == {"hr_bpm": None, "skin_temp_c": None}
    assert track["psi_est"] is None
    engine.ingest(_pkt(ptype=PacketType.CHECKIN, code=AlertCode.NONE, hr_bpm=None, skin_temp_c=22), 1, PACKET_SIZE)
    track = engine.snapshot(1)["soldiers"][0]
    assert track["baseline"] == {"hr_bpm": None, "skin_temp_c": 22.0}
    assert track["psi_est"] is None
    engine.ingest(_pkt(ptype=PacketType.CHECKIN, code=AlertCode.NONE, hr_bpm=70, skin_temp_c=22), 2, PACKET_SIZE)
    assert engine.snapshot(2)["soldiers"][0]["baseline"]["hr_bpm"] == 70
    engine.ingest(missing, 3, PACKET_SIZE)
    assert engine.snapshot(3)["soldiers"][0]["psi_est"] is None


def test_unavailable_position_is_not_a_map_point():
    p = decode(encode(_pkt(ptype=PacketType.CHECKIN, code=AlertCode.NONE, x_m=None, y_m=None)))
    assert p.x_m is None and p.y_m is None
    engine = Engine()
    engine.ingest(p, 1, PACKET_SIZE)
    soldier = engine.snapshot(1)["soldiers"][0]
    assert soldier["sector"] is None
    assert soldier["trail"] == []
    assert soldier["x_m"] is None and soldier["y_m"] is None


def test_missing_position_does_not_create_a_spatial_cluster():
    engine = Engine()
    engine.ingest(_pkt(soldier_id=1, x_m=None, y_m=None), 0, PACKET_SIZE)
    engine.ingest(_pkt(soldier_id=2, x_m=None, y_m=None), 1, PACKET_SIZE)
    assert engine.snapshot(1)["squad_events"] == []


def test_live_checkins_replace_values_and_expire(monkeypatch):
    from app import config
    monkeypatch.setattr(config, "STALE_AFTER_S", 5.0)
    engine = Engine()
    for second in range(1, 5):
        packet = decode(encode(_pkt(
            ptype=PacketType.CHECKIN, code=AlertCode.NONE, seq=second,
            t_s=second, hr_bpm=70 + second, skin_temp_c=22 + second / 10,
            activity=second, x_m=second, y_m=-second)))
        engine.ingest(packet, second, PACKET_SIZE)
        live = engine.snapshot(second)["soldiers"][0]
        assert (live["hr_bpm"], live["skin_temp_c"], live["activity"],
                live["x_m"], live["y_m"]) == (
                    70 + second, 22 + second / 10, second, second, -second)
        assert not live["stale"]
    missing = decode(encode(_pkt(ptype=PacketType.CHECKIN, code=0, seq=5,
                                hr_bpm=None, skin_temp_c=None)))
    engine.ingest(missing, 5, PACKET_SIZE)
    live = engine.snapshot(5)["soldiers"][0]
    assert live["hr_bpm"] is None and live["skin_temp_c"] is None
    assert not engine.snapshot(10)["soldiers"][0]["stale"]
    assert engine.snapshot(10.01)["soldiers"][0]["stale"]


def test_heart_rate_hold_is_display_only_and_expires():
    engine = Engine()
    checkin = dict(ptype=PacketType.CHECKIN, code=AlertCode.NONE)
    engine.ingest(_pkt(**checkin, hr_bpm=75), 1, PACKET_SIZE)
    live = engine.snapshot(1)["soldiers"][0]
    assert live["hr_status"] == "live" and live["hr_display_bpm"] == 75
    for second in range(2, 10):
        engine.ingest(_pkt(**checkin, hr_bpm=None), second, PACKET_SIZE)
        state = engine.snapshot(second)["soldiers"][0]
        assert state["hr_bpm"] is None and state["psi_est"] is None
        assert state["hr_display_bpm"] == 75 and state["hr_status"] == "reacquiring"
        assert state["severity"] == "OK" and not engine.alerts
    state = engine.snapshot(9.01)["soldiers"][0]
    assert state["hr_display_bpm"] is None and state["hr_status"] == "unavailable"
    engine.ingest(_pkt(**checkin, hr_bpm=81), 10, PACKET_SIZE)
    assert engine.snapshot(10)["soldiers"][0]["hr_display_bpm"] == 81
    assert engine.snapshot(10)["soldiers"][0]["hr_status"] == "live"


def test_missing_hr_cannot_create_hr_alert_but_sos_is_preserved():
    engine = Engine()
    for code in (AlertCode.HR_LOW_IMMOBILE, AlertCode.HR_SPIKE_IMMOBILE, AlertCode.HEAT_STRAIN):
        assert engine.ingest(_pkt(code=code, hr_bpm=None), 1, PACKET_SIZE) == []
    assert not engine.alerts
    assert engine.ingest(_pkt(code=AlertCode.SITREP_SOS, hr_bpm=None), 2, PACKET_SIZE)
    assert engine.snapshot(2)["soldiers"][0]["severity"] == "CRITICAL"


def test_lost_radio_does_not_show_live_or_held_hr(monkeypatch):
    from app import config
    monkeypatch.setattr(config, "STALE_AFTER_S", 5.0)
    engine = Engine()
    engine.ingest(_pkt(ptype=PacketType.CHECKIN, code=0, hr_bpm=75), 0, PACKET_SIZE)
    state = engine.snapshot(5.01)["soldiers"][0]
    assert state["hr_bpm"] is None and state["hr_display_bpm"] is None
    assert state["hr_status"] == "unavailable" and state["severity"] == "UNKNOWN"
