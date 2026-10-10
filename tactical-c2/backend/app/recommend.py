"""Fixed action catalog for operator recommendations.

The LLM may only pick actions from this catalog and may only target soldiers
and sectors that appear in the facts. Anything else is dropped. A
deterministic rule recommender produces the same shape when the LLM is off,
unreachable, or returns nothing valid, so the operator always gets an answer.
"""
from __future__ import annotations

import re

from .protocol import ALERT_LABELS, AlertCode

# target: "soldiers" = list of callsigns, "sector" = one sector, "none" = whole squad
CATALOG: dict[str, dict] = {
    "MEDEVAC": {"label": "Medical evacuation", "target": "soldiers",
                "use_when": "a soldier is down: 'Fall, then no movement', 'Low HR, no movement' or 'SOS' in active_label or alerts_last_2min (not for 'HR spike' alone)",
                "effect": "Casualty is carried back to the commander post (CP) by the nearest soldier."},
    "BUDDY_AID": {"label": "Buddy aid", "target": "soldiers",
                  "use_when": "a CRITICAL soldier needs first aid now and no squad event says contact",
                  "effect": "Nearest unaffected soldier moves to the casualty and gives first aid in place."},
    "REST_AND_COOL": {"label": "Rest and cool down", "target": "soldiers",
                      "use_when": "'Heat strain' or WATCH (rising strain) on a soldier who is still moving",
                      "effect": "Soldier stops, sheds load and rests in shade until heart rate and skin temperature fall."},
    "WITHDRAW_FROM_SECTOR": {"label": "Withdraw from sector", "target": "sector",
                             "use_when": "a squad event POSSIBLE_CONTACT or HAZARD_AREA names the sector",
                             "effect": "Everyone in the sector moves back to a rally point toward the CP."},
    "REGROUP_AT_CP": {"label": "Regroup at CP", "target": "none",
                      "use_when": "the squad is spread out and several soldiers are flagged",
                      "effect": "Whole squad moves back to the commander post."},
    "HOLD_AND_OBSERVE": {"label": "Hold and observe", "target": "none",
                         "use_when": "nothing is flagged, or only WATCH",
                         "effect": "No movement; keep monitoring and re-brief on the next alert."},
}

_SECTOR = re.compile(r"^[A-C][1-3]$")
_CALLSIGN = re.compile(r"\bS(\d{1,3})\b")

_CASUALTY = {AlertCode.FALL_IMMOBILE, AlertCode.HR_LOW_IMMOBILE, AlertCode.SITREP_SOS}
_LABEL_CODE = {v: k for k, v in ALERT_LABELS.items()}


def catalog_for_prompt() -> list[dict]:
    return [{"action": k, "target": v["target"], "use_when": v["use_when"]} for k, v in CATALOG.items()]


def _known(facts: dict) -> tuple[set[str], set[str], set[str]]:
    flagged = {s["callsign"] for s in facts["flagged_soldiers"]}
    squad = set(facts.get("squad_callsigns", [])) | flagged
    sectors = {s["sector"] for s in facts["flagged_soldiers"] if s.get("sector")}
    for e in facts["squad_events"]:
        squad.update(e["callsigns"])
        flagged.update(e["callsigns"])
        sectors.add(e["sector"])
    return flagged, squad, sectors


def validate(raw: list, facts: dict, limit: int = 3) -> tuple[list[dict], list[str]]:
    """Keep only catalog actions with targets that exist in the facts. Returns (valid, rejected reasons)."""
    flagged, squad, sectors = _known(facts)
    valid, rejected, seen = [], [], set()
    for item in raw if isinstance(raw, list) else []:
        if not isinstance(item, dict):
            rejected.append("not an object")
            continue
        action = str(item.get("action", "")).strip().upper()
        spec = CATALOG.get(action)
        if spec is None:
            rejected.append(f"unknown action {action or '?'}")
            continue
        targets: list[str] = []
        sector = None
        if spec["target"] == "soldiers":
            t = item.get("targets") or []
            t = [t] if isinstance(t, str) else t
            targets = sorted({str(x).strip().upper() for x in t if isinstance(x, (str, int))},
                             key=lambda c: int(c[1:]) if c[1:].isdigit() else 999)
            if not targets or not set(targets) <= flagged:
                rejected.append(f"{action}: targets {targets} not among flagged soldiers")
                continue
            by = {s["callsign"]: s for s in facts["flagged_soldiers"]}
            fits = [c for c in targets if _target_fits(action, by.get(c))]
            if not fits:
                rejected.append(f"{action}: {', '.join(targets)} {_TARGET_RULE[action]}")
                continue
            if len(fits) < len(targets):
                dropped = [c for c in targets if c not in fits]
                rejected.append(f"{action}: dropped {', '.join(dropped)} ({_TARGET_RULE[action]})")
                item = dict(item, reason="" if set(_CALLSIGN.findall(str(item.get("reason")))) & {c[1:] for c in dropped}
                            else item.get("reason"))
                targets = fits
        elif spec["target"] == "sector":
            sector = str(item.get("sector") or "").strip().upper()
            event_sectors = sorted({e["sector"] for e in facts["squad_events"]})
            if not sector and len(event_sectors) == 1:
                sector = event_sectors[0]
                rejected.append(f"{action}: sector missing, filled from the only squad event ({sector})")
            if not _SECTOR.match(sector) or sector not in sectors:
                rejected.append(f"{action}: sector {sector or '?'} not in the facts")
                continue
        why_not = _not_applicable(action, targets, sector, facts)
        if why_not:
            rejected.append(f"{action}: {why_not}")
            continue
        reason = str(item.get("reason") or "").strip()[:240]
        if {f"S{m}" for m in _CALLSIGN.findall(reason)} - squad:
            reason = ""  # reason mentions a soldier that does not exist: drop the text, keep the action
        key = (action, tuple(targets), sector)
        if key in seen:
            continue
        seen.add(key)
        valid.append({"action": action, "label": spec["label"], "targets": targets, "sector": sector,
                      "reason": reason, "effect": spec["effect"]})
        if len(valid) >= limit:
            break
    return valid, rejected


def _codes(s: dict) -> set:
    """Alert codes behind a flagged soldier: the active one and any in the last 2 min."""
    labels = [s["active_label"], *s.get("alerts_last_2min", [])]
    return {_LABEL_CODE[l] for l in labels if l in _LABEL_CODE}


_TARGET_RULE = {
    "MEDEVAC": "not down (needs fall / low HR / SOS)",
    "BUDDY_AID": "not CRITICAL",
    "REST_AND_COOL": "has no heat strain",
}


def _target_fits(action: str, s: dict | None) -> bool:
    if s is None:
        return False
    if action == "MEDEVAC":
        return bool(_codes(s) & _CASUALTY)
    if action == "BUDDY_AID":
        return s["severity"] == "CRITICAL"
    if action == "REST_AND_COOL":
        return AlertCode.HEAT_STRAIN in _codes(s) or s["severity"] == "WATCH"
    return True


def _not_applicable(action: str, targets: list[str], sector: str | None, facts: dict) -> str | None:
    """Squad-level catalog preconditions, checked against the facts. None = applicable."""
    if action == "WITHDRAW_FROM_SECTOR":
        named = {e["sector"] for e in facts["squad_events"]}
        return None if sector in named else f"no squad event in {sector}"
    if action == "HOLD_AND_OBSERVE":
        hot = [s["callsign"] for s in facts["flagged_soldiers"] if s["severity"] in ("CRITICAL", "WARN")]
        return f"{', '.join(hot)} flagged CRITICAL/WARN" if hot else None
    return None


def merge(llm_recs: list[dict], rule_recs: list[dict], limit: int = 4) -> list[dict]:
    """LLM picks first; rule picks fill the gaps the LLM left (different action or new targets)."""
    out = [dict(r, source="llm") for r in llm_recs]
    covered = {(r["action"], t) for r in out for t in (r["targets"] or [r["sector"] or "*"])}
    for r in rule_recs:
        if len(out) >= limit:
            break
        if r["targets"]:
            new = [t for t in r["targets"] if (r["action"], t) not in covered]
            if not new:
                continue
            r = dict(r, targets=new, reason=r["reason"] if new == r["targets"] else "")
        elif (r["action"], r["sector"] or "*") in covered:
            continue
        out.append(dict(r, source="rules"))
        covered |= {(r["action"], t) for t in (r["targets"] or [r["sector"] or "*"])}
    return out


def rule_recommendations(facts: dict) -> list[dict]:
    """Deterministic fallback, same output shape as validate()."""
    recs: list[dict] = []
    casualties = [s for s in facts["flagged_soldiers"] if _codes(s) & _CASUALTY]
    contact = [e for e in facts["squad_events"] if e["kind"] == "POSSIBLE_CONTACT"]
    heat = [s for s in facts["flagged_soldiers"] if AlertCode.HEAT_STRAIN in _codes(s) and s not in casualties]
    spikes = [s for s in facts["flagged_soldiers"] if AlertCode.HR_SPIKE_IMMOBILE in _codes(s) and s not in casualties]

    if casualties:
        who = [s["callsign"] for s in casualties]
        why = "; ".join(f"{c['callsign']}: " + ", ".join(ALERT_LABELS[k].lower() for k in sorted(_codes(c) & _CASUALTY))
                        for c in casualties)
        recs.append({"action": "MEDEVAC", "targets": who, "reason": why + "."})
    for e in contact[:1]:
        recs.append({"action": "WITHDRAW_FROM_SECTOR", "sector": e["sector"],
                     "reason": f"Possible contact in {e['sector']} ({', '.join(e['callsigns'])})."})
    if casualties and not contact:
        recs.append({"action": "BUDDY_AID", "targets": [casualties[0]["callsign"]],
                     "reason": "Nearest soldier can reach the casualty before evacuation."})
    if spikes and not contact and not casualties:
        who = [s["callsign"] for s in spikes]
        recs.append({"action": "BUDDY_AID", "targets": who, "reason": f"{', '.join(who)}: HR spike without movement."})
    if heat:
        who = [s["callsign"] for s in heat]
        recs.append({"action": "REST_AND_COOL", "targets": who,
                     "reason": f"{', '.join(who)}: sustained heart-rate and skin-temperature rise."})
    if not recs:
        recs.append({"action": "HOLD_AND_OBSERVE", "reason": "No soldier flagged."})
    valid, _ = validate(recs, facts)
    return valid
