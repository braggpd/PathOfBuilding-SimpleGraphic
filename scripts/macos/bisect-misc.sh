#!/usr/bin/env bash
# Binary-search Data/Misc.lua hang line range via fresh process per probe.
# Requires engine built with POB_MAC_BISECT_MISC support (ui_api.cpp).
#
# Usage:
#   POB_ROOT=~/PoB-PoE2-build ./scripts/macos/bisect-misc.sh
#
# Env (optional):
#   POB_ROOT          PoB tree (default: ~/PoB-PoE2-build)
#   ENGINE_BUILD      Space-free engine build (default: ~/PoB-SimpleGraphic-build)
#   BISECT_LOG        Log file (default: /tmp/pob-bisect-misc.log)
#   BISECT_SCRIPT     Launch script (default: test_misc.lua — faster than full Launch.lua)
#   BISECT_TIMEOUT    Per-probe seconds (default: 120; Misc can hang at 100% CPU)
#
# Manual probe (exits right after Misc slice):
#   POB_MAC_BISECT_MISC=1 POB_MAC_BISECT_LINES=1-50 \
#     ./runtime-macos/"Path of Building-PoE2" ./src/test_misc.lua
#
# Full PLoad path (slower, matches production):
#   BISECT_SCRIPT=Launch.lua ./scripts/macos/bisect-misc.sh

set -euo pipefail

POB_ROOT="${POB_ROOT:-$HOME/PoB-PoE2-build}"
ENGINE_BUILD="${ENGINE_BUILD:-$HOME/PoB-SimpleGraphic-build}"
BISECT_LOG="${BISECT_LOG:-/tmp/pob-bisect-misc.log}"
BISECT_SCRIPT="${BISECT_SCRIPT:-test_misc.lua}"
BISECT_TIMEOUT="${BISECT_TIMEOUT:-120}"
MISC="${POB_ROOT}/src/Data/Misc.lua"
HOST="${POB_ROOT}/runtime-macos/Path of Building-PoE2"
LAUNCH="${POB_ROOT}/src/${BISECT_SCRIPT}"
ENGINE_REPO="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_MISC_SRC="${ENGINE_REPO}/scripts/macos/test_misc.lua"

if [[ ! -f "$MISC" ]]; then
  echo "Missing $MISC" >&2
  exit 1
fi

if [[ ! -f "$LAUNCH" ]]; then
  if [[ "$BISECT_SCRIPT" == "test_misc.lua" && -f "$TEST_MISC_SRC" ]]; then
    echo "Installing $LAUNCH from engine repo..."
    cp "$TEST_MISC_SRC" "$LAUNCH"
  else
    echo "Missing $LAUNCH" >&2
    exit 1
  fi
fi

echo "Syncing engine sources..."
cp "$ENGINE_REPO"/ui_api.cpp "$ENGINE_REPO"/ui_main.cpp "$ENGINE_REPO"/ui_main.h "$ENGINE_BUILD"/
ninja -C "$ENGINE_BUILD/build"
cmake --install "$ENGINE_BUILD/build" --prefix "$POB_ROOT/runtime-macos"

TOTAL=$(wc -l < "$MISC" | tr -d ' ')
low=0
high="$TOTAL"
export POB_MAC_BISECT_MISC=1

probe() {
  local end="$1"
  : >"$BISECT_LOG"
  if python3 - <<PY >>"$BISECT_LOG" 2>&1
import os, subprocess, sys
env = os.environ.copy()
env["POB_MAC_BISECT_MISC"] = "1"
env["POB_MAC_BISECT_END"] = "$end"
try:
    rc = subprocess.run(["$HOST", "$LAUNCH"], env=env, timeout=int("$BISECT_TIMEOUT")).returncode
except subprocess.TimeoutExpired:
    rc = 124
sys.exit(rc)
PY
  then
    return 0
  fi
  local rc=$?
  # Syntax-only failure from snapping prefix: treat as OK boundary for search.
  if grep -q "BISECT: load lines" "$BISECT_LOG" && ! grep -q "macOS BISECT: Misc.lua slice OK" "$BISECT_LOG"; then
    return 0
  fi
  return "$rc"
}

echo "Misc.lua has $TOTAL lines. Probing prefixes 1..N (one process per probe, timeout ${BISECT_TIMEOUT}s)."
echo "Script: $LAUNCH"
echo "Log: $BISECT_LOG"

while (( low + 1 < high )); do
  mid=$(( (low + high) / 2 ))
  echo ""
  echo "=== BISECT: lines 1-$mid ==="
  if probe "$mid"; then
    echo "OK   lines 1-$mid"
    low=$mid
  else
    rc=$?
    echo "FAIL lines 1-$mid (exit $rc)"
    tail -n 20 "$BISECT_LOG" || true
    high=$mid
  fi
done

echo ""
echo "=== BISECT result ==="
echo "Last good prefix: lines 1-$low"
echo "First failing line is in: $((low + 1)) .. $high"
echo ""
echo "LLDB (separate terminal):"
echo "  cd \"$POB_ROOT\""
echo "  POB_MAC_BISECT_MISC=1 POB_MAC_BISECT_LINES=$((low + 1))-$high lldb -- ./runtime-macos/\"Path of Building-PoE2\" ./src/${BISECT_SCRIPT}"
