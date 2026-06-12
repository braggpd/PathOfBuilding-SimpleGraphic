#!/usr/bin/env bash
# Binary-search Data.lua data.misc block (lines 171-248) via PLoad tail on root.
#
# Usage:
#   POB_ROOT=~/PoB-PoE2-build ./scripts/macos/bisect-data-misc.sh
#
# Manual probe:
#   POB_MAC_BISECT_DATA_MISC=1 POB_MAC_BISECT_DATA_END=200 \
#     ./runtime-macos/"Path of Building-PoE2" ./src/Launch_oninit_pload.lua

set -euo pipefail

POB_ROOT="${POB_ROOT:-$HOME/PoB-PoE2-build}"
ENGINE_BUILD="${ENGINE_BUILD:-$HOME/PoB-SimpleGraphic-build}"
BISECT_LOG="${BISECT_LOG:-/tmp/pob-bisect-data-misc.log}"
BISECT_SCRIPT="${BISECT_SCRIPT:-Launch_oninit_pload.lua}"
BISECT_TIMEOUT="${BISECT_TIMEOUT:-90}"
DATA="${POB_ROOT}/src/Modules/Data.lua"
HOST="${POB_ROOT}/runtime-macos/Path of Building-PoE2"
LAUNCH="${POB_ROOT}/src/${BISECT_SCRIPT}"
ENGINE_REPO="$(cd "$(dirname "$0")/../.." && pwd)"

MISC_START=171
MISC_END=248

if [[ ! -f "$DATA" ]]; then
  echo "Missing $DATA" >&2
  exit 1
fi

if [[ ! -f "$LAUNCH" ]]; then
  echo "Missing $LAUNCH" >&2
  exit 1
fi

echo "Syncing engine sources..."
cp "$ENGINE_REPO"/ui_api.cpp "$ENGINE_REPO"/ui_main.cpp "$ENGINE_REPO"/ui_main.h "$ENGINE_BUILD"/
ninja -C "$ENGINE_BUILD/build"
cmake --install "$ENGINE_BUILD/build" --prefix "$POB_ROOT/runtime-macos"

low=$((MISC_START - 1))
high="$MISC_END"

probe() {
  local end="$1"
  pkill -9 -f "Path of Building" 2>/dev/null || true
  : >"$BISECT_LOG"
  python3 - <<PY >>"$BISECT_LOG" 2>&1
import os, subprocess, sys
env = os.environ.copy()
env["POB_MAC_BISECT_DATA_MISC"] = "1"
env["POB_MAC_BISECT_DATA_END"] = "$end"
try:
    r = subprocess.run(
        ["$HOST", "$LAUNCH"],
        env=env,
        timeout=int("$BISECT_TIMEOUT"),
        capture_output=True,
        text=True,
    )
    sys.stdout.write(r.stdout or "")
    sys.stderr.write(r.stderr or "")
    rc = r.returncode
except subprocess.TimeoutExpired as e:
    sys.stdout.write(e.stdout or "")
    sys.stderr.write(e.stderr or "")
    rc = 124
sys.exit(0 if rc == 0 else 1)
PY
  if grep -q "macOS BISECT: data.misc tail OK" "$BISECT_LOG"; then
    return 0
  fi
  if grep -q "PLoad Data.lua tail load error" "$BISECT_LOG"; then
  # Syntax-only failure from mid-table slice: treat as OK boundary.
    return 0
  fi
  return 1
}

echo "data.misc block: lines $MISC_START-$MISC_END in Data.lua"
echo "Script: $LAUNCH (timeout ${BISECT_TIMEOUT}s). Log: $BISECT_LOG"

while (( low + 1 < high )); do
  mid=$(( (low + high) / 2 ))
  echo ""
  echo "=== BISECT: data.misc lines $MISC_START-$mid ==="
  if probe "$mid"; then
    echo "OK   through line $mid"
    low=$mid
  else
    echo "FAIL through line $mid"
    tail -n 20 "$BISECT_LOG" || true
    high=$mid
  fi
done

echo ""
echo "=== BISECT result ==="
echo "Last good end line: $low"
echo "First bad line region: $((low + 1)) .. $high"
echo ""
echo "Manual confirm:"
echo "  cd \"$POB_ROOT\""
echo "  POB_MAC_BISECT_DATA_MISC=1 POB_MAC_BISECT_DATA_END=$high \\"
echo "    ./runtime-macos/\"Path of Building-PoE2\" ./src/${BISECT_SCRIPT}"
