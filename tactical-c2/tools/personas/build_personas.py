"""Build soldier personas for the /example page from a public wearable dataset.

Source (open access, ODC-By 1.0):
  Hongn A, Bosch F, Prado L, Bonomini P. Wearable Device Dataset from Induced Stress and
  Structured Exercise Sessions (version 1.0.1). PhysioNet (2025). doi:10.13026/he0v-tf17
  Original paper: Hongn et al., Scientific Data 12 (2025). doi:10.1038/s41597-025-04845-9

Empatica E4 wristband per subject and session (STRESS / AEROBIC / ANAEROBIC):
  HR.csv 1 Hz, TEMP.csv 4 Hz (skin, degC), ACC.csv 32 Hz (x,y,z in 1/64 g), tags.csv (UTC marks).
Each file: row 1 = UTC start time, row 2 = sample rate, then samples.

Output (backend/app/data/personas.json), all channels resampled to 1 Hz on the HR clock:
  hr [bpm], temp [degC x10], act [0..255, mean |(|a| - 1 g)| per second, 1 g = 255, same scale
  as the radio packet], peak [|a| max per second, g x10], tags [s from HR start].

Needs internet once (run on the commander laptop). Usage:
  python tools/personas/build_personas.py            # default 8 subjects
  python tools/personas/build_personas.py S01 S04 ...
"""
from __future__ import annotations

import csv
import io
import json
import math
import statistics
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

BASE = "https://physionet.org/files/wearable-device-dataset/1.0.1/"
SESSIONS = {"stress": "STRESS", "aerobic": "AEROBIC", "anaerobic": "ANAEROBIC"}
# Stage-1 subjects with complete stress/aerobic/anaerobic sessions (see data_constraints.txt:
# S02 stress duplicated, S03/S07 aerobic incomplete, S06 anaerobic incomplete, S11/S16 split, S12 missing).
DEFAULT_SUBJECTS = ["S01", "S04", "S05", "S08", "S09", "S10", "S13", "S14", "S15", "S17", "S18"]
WANT = 8
MAX_LEN_S = 2700
OUT = Path(__file__).resolve().parents[2] / "backend" / "app" / "data" / "personas.json"

SOURCE = {
    "title": "Wearable Device Dataset from Induced Stress and Structured Exercise Sessions",
    "version": "1.0.1",
    "authors": "Andrea Hongn, Facundo Bosch, Lara Prado, Paula Bonomini",
    "publisher": "PhysioNet",
    "year": 2025,
    "doi": "10.13026/he0v-tf17",
    "url": "https://physionet.org/content/wearable-device-dataset/1.0.1/",
    "paper_doi": "10.1038/s41597-025-04845-9",
    "license": "Open Data Commons Attribution License v1.0 (ODC-By)",
    "device": "Empatica E4 wristband",
    "note": "Resampled to 1 Hz; values unchanged otherwise. Scenario overlays are applied at run time and labelled.",
}


def fetch(path: str, retries: int = 3) -> str:
    url = BASE + path
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                return r.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise FileNotFoundError(url) from e
            if attempt == retries - 1:
                raise
        except urllib.error.URLError:
            if attempt == retries - 1:
                raise
        time.sleep(2 * (attempt + 1))
    raise RuntimeError(url)


def _ts(s: str) -> float:
    return datetime.strptime(s.strip()[:19], "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc).timestamp()


def signal(text: str) -> tuple[float, float, list[list[float]]]:
    rows = [r for r in csv.reader(io.StringIO(text)) if r and r[0].strip()]
    start = float(rows[0][0]) if _is_num(rows[0][0]) else _ts(rows[0][0])
    rate = float(rows[1][0])
    data = [[float(v) for v in r] for r in rows[2:] if all(_is_num(v) for v in r)]
    return start, rate, data


def _is_num(v: str) -> bool:
    try:
        float(v)
        return True
    except ValueError:
        return False


def per_second(start: float, rate: float, values: list[float], t0: float, n: int, agg) -> list[float | None]:
    out: list[float | None] = []
    for sec in range(n):
        a = int(round((t0 + sec - start) * rate))
        b = int(round((t0 + sec + 1 - start) * rate))
        chunk = values[max(a, 0):max(b, 0)]
        out.append(agg(chunk) if chunk else None)
    return out


def fill(xs: list[float | None]) -> list[float]:
    last = next((x for x in xs if x is not None), 0.0)
    out = []
    for x in xs:
        last = x if x is not None else last
        out.append(last)
    return out


def session(subject: str, folder: str) -> dict:
    d = f"Wearable_Dataset/{folder}/{subject}/"
    hr_start, _, hr_rows = signal(fetch(d + "HR.csv"))
    t_start, t_rate, t_rows = signal(fetch(d + "TEMP.csv"))
    a_text = fetch(d + "ACC.csv")
    a_rows_raw = [r for r in csv.reader(io.StringIO(a_text)) if r and r[0].strip()]
    a_start = _ts(a_rows_raw[0][0]) if not _is_num(a_rows_raw[0][0]) else float(a_rows_raw[0][0])
    a_rate = float(a_rows_raw[1][0])
    mags = [math.sqrt(sum((float(v) / 64.0) ** 2 for v in r[:3])) for r in a_rows_raw[2:] if len(r) >= 3 and all(_is_num(v) for v in r[:3])]
    try:
        tags = [round(_ts(r[0]) - hr_start) for r in csv.reader(io.StringIO(fetch(d + "tags.csv"))) if r and r[0].strip()]
    except FileNotFoundError:
        tags = []
    hr = [r[0] for r in hr_rows]
    n = min(len(hr), MAX_LEN_S)
    temp = fill(per_second(t_start, t_rate, [r[0] for r in t_rows], hr_start, n, statistics.fmean))
    act = fill(per_second(a_start, a_rate, mags, hr_start, n,
                          lambda c: min(255.0, statistics.fmean(abs(m - 1.0) for m in c) * 255)))
    peak = fill(per_second(a_start, a_rate, mags, hr_start, n, max))
    return {
        "start_utc": datetime.fromtimestamp(hr_start, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "tags": [t for t in tags if 0 <= t < n],
        "hr": [round(v) for v in hr[:n]],
        "temp": [round(v * 10) for v in temp],
        "act": [round(v) for v in act],
        "peak": [round(v * 10) for v in peak],
    }


def subject_info() -> dict[str, dict]:
    rows = list(csv.reader(io.StringIO(fetch("subject-info.csv"))))
    out = {}
    for r in rows[1:]:
        if not r or not r[0].strip().startswith(("S", "f")) or len(r) < 6:
            continue
        sid = r[0].strip()
        try:
            out[sid] = {"sex": r[1].strip(), "age": int(r[2]), "height_cm": int(float(r[3])),
                        "weight_kg": int(float(r[4])), "regular_activity": r[5].strip().lower().startswith("y")}
        except ValueError:
            continue
    return out


def main(argv: list[str]) -> None:
    wanted = argv or DEFAULT_SUBJECTS
    info = subject_info()
    personas = []
    for sid in wanted:
        if len(personas) >= (len(argv) or WANT):
            break
        if sid not in info:
            print(f"{sid}: not in subject-info, skipped")
            continue
        try:
            sessions = {k: session(sid, folder) for k, folder in SESSIONS.items()}
        except (FileNotFoundError, IndexError, ValueError) as e:
            print(f"{sid}: skipped ({e})")
            continue
        base = sessions["stress"]
        rest_hr = statistics.median(base["hr"][:150])
        rest_temp = statistics.median(base["temp"][:150]) / 10
        personas.append({"subject": sid, **info[sid], "resting_hr": round(rest_hr),
                         "resting_temp_c": round(rest_temp, 1), "sessions": sessions})
        print(f"{sid}: ok  rest HR {rest_hr:.0f}  rest temp {rest_temp:.1f}  "
              + "  ".join(f"{k} {len(v['hr'])}s" for k, v in sessions.items()))
    if not personas:
        sys.exit("no personas built")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({"source": SOURCE, "personas": personas}, separators=(",", ":")), encoding="utf-8")
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} KB, {len(personas)} personas)")


if __name__ == "__main__":
    main(sys.argv[1:])
