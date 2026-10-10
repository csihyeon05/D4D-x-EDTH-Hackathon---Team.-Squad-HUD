#!/bin/sh
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/nu40-gateway.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer test_gateway_core.cpp -o "$binary"
"$binary"
if [ -n "${BACKEND_ROOT:-}" ]; then
  "$binary" --emit | PYTHONPATH="$BACKEND_ROOT" python3 -c '
import sys
from app.protocol import parse_gateway_line
packets = [parse_gateway_line(line) for line in sys.stdin]
assert len(packets) == 2 and all(packets)
assert packets[0].soldier_id == 3 and packets[0].hr_bpm == 141
assert packets[1].soldier_id == 255 and packets[1].rssi == -128
print("Backend parser accepted NU40 gateway vectors")
'
fi
