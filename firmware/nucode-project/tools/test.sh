#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_binary=$(mktemp /tmp/nucode-tests.XXXXXX)
dmp_binary="${test_binary}.dmp"
node_binary="${test_binary}.node"
raw_binary="${test_binary}.raw"
lsm_binary="${test_binary}.lsm"
hw_binary="${test_binary}.hw"
heading_binary="${test_binary}.heading"
trap 'rm -f "$test_binary" "$dmp_binary" "$node_binary" "$raw_binary" "$lsm_binary" "$hw_binary" "$heading_binary"' EXIT HUP INT TERM
flags="-std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DSOLDIER_ID=1 -Itest/native -Isrc"
c++ $flags \
  test/native/test_firmware.cpp src/sensors/nmea_parser.cpp \
  src/sensors/pulse_sensor.cpp src/sensors/tmp36.cpp src/comm/i2c_bus.cpp src/core/status_led.cpp \
  -o "$test_binary"
"$test_binary"
c++ $flags -DCFG_IMU_DMP=1 test/native/test_dmp.cpp src/sensors/imu.cpp -o "$dmp_binary"
"$dmp_binary"
c++ $flags test/native/test_node.cpp src/node/rules.cpp src/node/motion.cpp \
  src/node/position.cpp -o "$node_binary"
"$node_binary"
c++ $flags test/native/test_dmp.cpp src/sensors/imu.cpp -o "$raw_binary"
"$raw_binary"
c++ $flags -DCFG_IMU_MODEL=2 test/native/test_lsm6ds3.cpp src/sensors/imu.cpp -o "$lsm_binary"
"$lsm_binary"
c++ $flags -Itest/hardware -DBOARD_AMB26 test/hardware/test_hardware.cpp src/comm/i2c_hardware.cpp -o "$hw_binary"
"$hw_binary"
c++ $flags test/native/test_raw_heading.cpp src/node/motion.cpp -o "$heading_binary"
"$heading_binary"
NODE_BINARY="$node_binary" python3 -m unittest discover -s test -p 'test_*.py' -v
