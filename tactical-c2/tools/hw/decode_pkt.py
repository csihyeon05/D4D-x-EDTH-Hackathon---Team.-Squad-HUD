"""Decode gateway lines with the backend parser.
Usage: python decode_pkt.py "PKT:1202ab7650060000a408070000000000007d,RSSI:-69" [...]
   or: python decode_pkt.py < gateway.log
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "backend"))
from app.protocol import parse_gateway_line  # noqa: E402

lines = sys.argv[1:] or [l for l in sys.stdin.read().splitlines() if l.startswith("PKT:")]
for line in lines:
    try:
        print(parse_gateway_line(line.strip()))
    except Exception as exc:  # malformed line or bad CRC
        print("REJECT", line.strip(), exc)
