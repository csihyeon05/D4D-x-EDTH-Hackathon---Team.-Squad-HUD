#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root=$(pwd)
cli=${ARDUINO_CLI:-}
if [ -z "$cli" ]; then
  if command -v arduino-cli >/dev/null 2>&1; then cli=$(command -v arduino-cli)
  else cli="$HOME/.local/bin/arduino-cli"; fi
fi
if [ ! -x "$cli" ]; then echo "Install Arduino CLI or set ARDUINO_CLI to its path." >&2; exit 127; fi
index=https://raw.githubusercontent.com/Ameba-AIoT/ameba-arduino-d/master/Arduino_package/package_realtek_amebad_index.json
fqbn=realtek:AmebaD:Ameba_AMB26:AutoUploadMode=Enable,UploadBaudrate=921600
case "${1:-build}" in
  setup)
    "$cli" core update-index --additional-urls "$index"
    exec "$cli" core install realtek:AmebaD@3.1.9 --additional-urls "$index"
    ;;
  build|upload)
    case "${SOLDIER_ID:-}" in
      ''|*[!0-9]*) echo 'Set SOLDIER_ID to an integer 1..255.' >&2; exit 2 ;;
    esac
    if [ "$SOLDIER_ID" -lt 1 ] || [ "$SOLDIER_ID" -gt 255 ]; then exit 2; fi
    case "${IMU_MODEL:-mpu6050}" in
      mpu6050) model=1 ;;
      lsm6ds3) model=2 ;;
      *) echo 'IMU_MODEL must be mpu6050 or lsm6ds3.' >&2; exit 2 ;;
    esac
    defines="-DSOLDIER_ID=$SOLDIER_ID -DCFG_IMU_MODEL=$model"
    for option in CFG_IMU_DMP CFG_TMP36 CFG_PULSE CFG_GPS CFG_PULSE_TRACE; do
      value=$(printenv "$option" || true)
      case "$value" in
        '') ;;
        0|1) defines="$defines -D$option=$value" ;;
        *) echo "$option must be 0 or 1." >&2; exit 2 ;;
      esac
    done
    if [ -n "${AO_ORIGIN_LAT_E7:-}${AO_ORIGIN_LON_E7:-}" ]; then
      defines="$defines -DAO_ORIGIN_LAT_E7=${AO_ORIGIN_LAT_E7:-} -DAO_ORIGIN_LON_E7=${AO_ORIGIN_LON_E7:-}"
    fi
    # Vendor recipes do not reliably quote paths containing spaces.
    # Stage in a private space-free directory; keep final artifacts in the project.
    build_root=$(mktemp -d /tmp/nu87-build.XXXXXX)
    trap 'rm -rf "$build_root"' EXIT HUP INT TERM
    mkdir -p "$build_root/nu87_hud" "$build_root/output" "$root/.build/output"
    cp -R src "$build_root/nu87_hud/src"
    printf '#include <BLEDevice.h>\n' > "$build_root/nu87_hud/nu87_hud.ino"
    # Realtek recipes ignore --input-file and use images in their tool directory.
    # Isolate that directory so another Arduino project cannot replace our image.
    properties=$("$cli" compile --fqbn "$fqbn" --show-properties=expanded "$build_root/nu87_hud")
    vendor_tools=$(printf '%s\n' "$properties" | sed -n 's/^runtime.tools.ameba_d_tools.path=//p')
    if [ ! -d "$vendor_tools" ]; then echo 'Cannot locate Realtek image tools.' >&2; exit 1; fi
    mkdir "$build_root/vendor-tools"
    cp -R "$vendor_tools/." "$build_root/vendor-tools/"
    "$cli" compile --fqbn "$fqbn" --build-property "runtime.tools.ameba_d_tools.path=$build_root/vendor-tools" --build-path "$build_root/output" \
      --build-property "compiler.c.extra_flags=-I$build_root/output/sketch/src $defines" "$build_root/nu87_hud"
    cp "$build_root/output/km0_km4_image2.bin" "$root/.build/output/"
    if [ "${1:-build}" = upload ]; then
      if [ -z "${2:-}" ]; then echo 'Usage: tools/nu87.sh upload /dev/ttyUSB0' >&2; exit 2; fi
      # The vendor uploader can return zero even when opening the port fails.
      upload_log="$build_root/upload.log"
      if ! "$cli" upload --fqbn "$fqbn" --upload-property "runtime.tools.ameba_d_tools.path=$build_root/vendor-tools" --port "$2" --input-file "$build_root/output/km0_km4_image2.bin" "$build_root/nu87_hud" >"$upload_log" 2>&1; then
        cat "$upload_log"
        exit 1
      fi
      cat "$upload_log"
      if ! grep -q 'All images are sent successfully!' "$upload_log"; then
        echo 'Upload was not confirmed by the device uploader.' >&2
        exit 1
      fi
    fi
    ;;
  monitor)
    exec "$cli" monitor --port "${2:-/dev/ttyUSB0}" --config baudrate=9600
    ;;
  *) echo 'Usage: tools/nu87.sh {setup|build|upload PORT|monitor PORT}' >&2; exit 2 ;;
esac
