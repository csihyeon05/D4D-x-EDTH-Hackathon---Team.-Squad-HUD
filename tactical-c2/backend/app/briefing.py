"""Natural-language briefing.

Design rule: the analysis engine decides WHAT is true; the LLM only phrases it.
The LLM receives a compact facts JSON and must not add anything. If the local
LLM is unreachable, slow, or its output mentions a soldier that is not in the
facts, we fall back to a deterministic template, so the COP never shows an
unverified claim.
"""
from __future__ import annotations

import json
import re
import time

import httpx

from . import config, llm
from .protocol import AlertCode
from .recommend import _codes, catalog_for_prompt, merge, rule_recommendations, validate

SYSTEM_PROMPT = (
    "You are the briefing assistant of an offline squad command terminal. "
    "Write a tactical briefing for the squad leader in 2 to 4 short sentences. "
    "Use ONLY the facts in the JSON. Do not invent soldiers, locations, causes, or numbers. "
    "Keep words like 'suspected' and 'possible' exactly as cautious as the facts. "
    "Lead with the most critical item. Refer to soldiers by callsign and sector. "
    "If nothing is flagged, say all soldiers are nominal. "
    'Answer as JSON: {"brief": "<the briefing>"}'
)

RECOMMEND_PROMPT = (
    "You are the operator assistant of an offline squad command terminal. "
    "Read the FACTS JSON and answer as JSON with two keys.\n"
    '"brief": 2 to 3 short sentences for the squad leader. Start with the most severe soldiers. For every soldier you name, '
    "state only that soldier's own active_label or interpretation from the facts; never move a condition to another soldier. "
    "Mention squad_events. Keep 'suspected' and 'possible'.\n"
    '"recommendations": 1 to 3 objects, most urgent first. Pick an ACTION only if its use_when matches the facts. '
    'Target "soldiers": "targets" = only matching callsigns. Target "sector": "sector" = the sector in squad_events. '
    '"reason" = one short sentence quoting the matching fact.\n'
    "Recommendation format example (S98/S99 are not real, never copy them): "
    '[{"action": "MEDEVAC", "targets": ["S99"], "reason": "S99: fall, then no movement."}, '
    '{"action": "REST_AND_COOL", "targets": ["S98"], "reason": "S98: heat strain."}]'
)


def template_briefing(f: dict) -> str:
    parts = [f"{f['mission_time']}."]
    for e in f["squad_events"]:
        if e["kind"] == "POSSIBLE_CONTACT":
            parts.append(f"Possible contact in sector {e['sector']}: {', '.join(e['callsigns'])} "
                         f"broke silence together.")
        elif e["kind"] == "HAZARD_AREA":
            parts.append(f"Possible hazard area in sector {e['sector']} ({', '.join(e['callsigns'])}).")
    order = {"CRITICAL": 0, "WARN": 1, "UNKNOWN": 2, "WATCH": 3}
    # group soldiers sharing the same condition and sector to keep the briefing short
    groups: dict[tuple, list[dict]] = {}
    for s in sorted(f["flagged_soldiers"], key=lambda s: (order.get(s["severity"], 9), s["callsign"])):
        key = (s["severity"], s["active_label"] if s["severity"] in ("CRITICAL", "WARN") else "", s["sector"])
        groups.setdefault(key, []).append(s)
    for (sev, _label, sector), members in groups.items():
        who = ", ".join(m["callsign"] for m in members)
        s = members[0]
        if sev == "UNKNOWN":
            parts.append(f"{who}: no check-in received, status unknown.")
        elif sev == "WATCH":
            parts.append(f"{who} in {sector}: elevated physiological strain.")
        else:
            hr = "/".join(str(m["hr_bpm"]) for m in members)
            parts.append(f"{sev.title()}: {who} in {sector}, {s['interpretation']} (HR {hr}).")
    if len(parts) == 1:
        parts.append(f"All {f['squad_size']} soldiers nominal.")
    return " ".join(parts)


_CALLSIGN = re.compile(r"\bS(\d{1,3})\b")


_CLAIMS = {  # word in a sentence -> alert codes that must back it for a soldier named in that sentence
    "heat": {AlertCode.HEAT_STRAIN},
    "fall": {AlertCode.FALL_IMMOBILE},
    "consciousness": {AlertCode.HR_LOW_IMMOBILE},
    "sos": {AlertCode.SITREP_SOS},
    "gas": {AlertCode.HAZARD_ENV},
}


def _grounded(text: str, f: dict) -> bool:
    """Callsigns must exist, and a condition word in a sentence must match a named soldier's alerts."""
    allowed = {s["callsign"] for s in f["flagged_soldiers"]} | set(f.get("squad_callsigns", []))
    for e in f["squad_events"]:
        allowed.update(e["callsigns"])
    mentioned = {f"S{m}" for m in _CALLSIGN.findall(text)}
    if not mentioned <= allowed:
        return False
    by = {s["callsign"]: s for s in f["flagged_soldiers"]}
    for sentence in re.split(r"(?<=[.;!?])\s+", text):
        # soldiers not flagged count as having no alerts at all
        named = [by.get(c, {"severity": "OK", "active_label": "Check-in"})
                 for c in {f"S{m}" for m in _CALLSIGN.findall(sentence)}]
        if not named:
            continue
        low = sentence.lower()
        for word, codes in _CLAIMS.items():
            if word in low and not any(_codes(s) & codes for s in named):
                return False
        if "critical" in low and not any(s["severity"] == "CRITICAL" for s in named):
            return False
    return True


def _llm_error(exc: Exception) -> str:
    if isinstance(exc, httpx.ConnectError):
        return "Local sLLM not reachable; showing rule-based output"
    if isinstance(exc, httpx.TimeoutException):
        return "Local sLLM timed out; showing rule-based output"
    return f"sLLM error ({type(exc).__name__}); rule-based output"


class Briefer:
    def __init__(self) -> None:
        self.llm_ok: bool | None = None
        self.last_error: str | None = None

    async def brief(self, facts: dict) -> dict:
        t0 = time.perf_counter()
        text, source = None, "template"
        if config.LLM_ENABLED:
            try:
                text = await self._llm(facts)
                self.llm_ok, self.last_error = True, None
                if not text or not _grounded(text, facts):
                    self.last_error = "LLM briefing empty or failed the fact check; rule template shown"
                    text = None
                else:
                    source = "llm"
            except Exception as exc:  # unreachable, timeout, HTTP error, bad JSON
                self.llm_ok = not isinstance(exc, (httpx.ConnectError, httpx.TimeoutException))
                self.last_error = _llm_error(exc)
        if text is None:
            text = template_briefing(facts)
        return {
            "text": text,
            "source": source,
            "model": config.LLM_MODEL if source == "llm" else None,
            "latency_ms": round((time.perf_counter() - t0) * 1000),
            "facts": facts,
            "note": self.last_error,
        }

    async def _llm(self, facts: dict) -> str:
        out = await llm.generate_json(SYSTEM_PROMPT, "FACTS:\n" + json.dumps(facts, ensure_ascii=False),
                                      max_tokens=200)
        return str(out.get("brief") or "").strip()

    async def recommend(self, facts: dict) -> dict:
        """Briefing + up to 3 actions from the fixed catalog. Every item says where it came from."""
        t0 = time.perf_counter()
        brief, brief_src, recs, rec_src, rejected, note = None, "template", [], "rules", [], None
        if config.LLM_ENABLED:
            user = ("ACTIONS:\n" + json.dumps(catalog_for_prompt(), ensure_ascii=False)
                    + "\nFACTS:\n" + json.dumps(facts, ensure_ascii=False))
            try:
                out = await llm.generate_json(RECOMMEND_PROMPT, user, max_tokens=400)
                self.llm_ok, self.last_error = True, None
                text = out.get("brief") or ""
                text = " ".join(map(str, text)) if isinstance(text, list) else str(text)
                text = text.strip()
                if text and _grounded(text, facts):
                    brief, brief_src = text, "llm"
                elif text:
                    note = "LLM briefing failed the fact check (unknown soldier or a condition not in their alerts); rule template shown"
                recs, rejected = validate(out.get("recommendations") or [], facts)
                if recs:
                    rec_src = "llm"
                elif not note:
                    note = "LLM returned no valid catalog action; rule-based recommendations shown"
                if recs:
                    recs = merge(recs, rule_recommendations(facts))
                    if any(r["source"] == "rules" for r in recs):
                        rec_src = "llm+rules"
            except Exception as exc:
                self.llm_ok = not isinstance(exc, (httpx.ConnectError, httpx.TimeoutException))
                self.last_error = note = _llm_error(exc)
        if brief is None:
            brief = template_briefing(facts)
        if not recs:
            recs, rec_src = [dict(r, source="rules") for r in rule_recommendations(facts)], "rules"
        return {
            "text": brief,
            "source": brief_src,
            "recommendations": recs,
            "recommendation_source": rec_src,
            "rejected": rejected,
            "model": config.LLM_MODEL if "llm" in (brief_src, rec_src) else None,
            "latency_ms": round((time.perf_counter() - t0) * 1000),
            "facts": facts,
            "note": note,
        }
