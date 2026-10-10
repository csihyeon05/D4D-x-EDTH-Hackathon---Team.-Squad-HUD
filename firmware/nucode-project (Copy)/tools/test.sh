#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_binary=$(mktemp /tmp/nucode-tests.XXXXXX)
dmp_binary="${test_binary}.dmp"
raw_binary="${test_binary}.raw"
trap 'rm -f "$test_binary" "$dmp_binary" "$raw_binary"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Itest/native -Isrc \
  test/native/test_firmware.cpp src/sensors/nmea_parser.cpp \
  src/sensors/pulse_sensor.cpp src/sensors/tmp36.cpp src/comm/i2c_bus.cpp src/core/status_led.cpp \
  -o "$test_binary"
"$test_binary"
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Itest/native -Isrc \
  test/native/test_lsm6ds3.cpp src/sensors/imu.cpp -o "$raw_binary"
"$raw_binary"
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -DBOARD_AMB26 -Itest/hardware -Itest/native -Isrc \
  test/hardware/test_hardware.cpp src/comm/i2c_hardware.cpp -o "$dmp_binary"
"$dmp_binary"
python3 -m unittest discover -s test -p 'test_*.py' -v
