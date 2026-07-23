#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

JLINK="${JLINK:-JLinkExe}"
OPENOCD="${OPENOCD:-openocd}"
PRESET="${PRESET:-app-a}"
HEX="${HEX:-build/app-a/app/upboard_A.hex}"
PARAMS="${PARAMS:-}"

power() {
  local state="$1"
  local script
  script="$(mktemp /tmp/jlink-power.XXXXXX.jlink)"
  # First command may only establish the USB session on some J-Link versions.
  printf 'power %s\npower %s\nq\n' "$state" "$state" > "$script"
  "$JLINK" -NoGui 1 -CommanderScript "$script" >/dev/null
  rm -f "$script"
}

cleanup() {
  power off || true
}
trap cleanup EXIT

echo "[1/4] Build $PRESET while target power is off"
cmake --build --preset "$PRESET"

echo "[2/4] J-Link target power on"
power on

echo "[3/4] Flash $HEX"
if [[ -n "$PARAMS" ]]; then
  "$OPENOCD" \
    -f interface/jlink.cfg \
    -c "transport select swd" \
    -f target/stm32f4x.cfg \
    -c "init; reset halt" \
    -c "program $PARAMS 0x08008000 verify" \
    -c "program $HEX verify reset" \
    -c "exit"
else
  "$OPENOCD" \
    -f interface/jlink.cfg \
    -c "transport select swd" \
    -f target/stm32f4x.cfg \
    -c "init; reset halt" \
    -c "program $HEX verify reset" \
    -c "exit"
fi

echo "[4/4] Verified OK; powering off"
