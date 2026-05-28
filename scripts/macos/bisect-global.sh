#!/usr/bin/env bash
# Binary-search Data/Global.lua crash line range via fresh process per probe.
# Requires engine built with POB_MAC_BISECT_GLOBAL support (ui_api.cpp).
#
# Usage:
#   POB_ROOT=~/PoB-PoE2-build ./scripts/macos/bisect-global.sh
#
# Env (optional):
#   POB_ROOT          PoB tree (default: ~/PoB-PoE2-build)
#   ENGINE_BUILD      Space-free engine build (default: ~/PoB-SimpleGraphic-build)
#   BISECT_LOG        Log file (default: /tmp/pob-bisect.log)
#
# Manual probe (exits right after Global slice; no hang on Data/Misc.lua):
#   POB_MAC_BISECT_GLOBAL=1 POB_MAC_BISECT_LINES=300-350 \
#     ./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua

set -euo pipefail

POB_ROOT="${POB_ROOT:-$HOME/PoB-PoE2-build}"
ENGINE_BUILD="${ENGINE_BUILD:-$HOME/PoB-SimpleGraphic-build}"
BISECT_LOG="${BISECT_LOG:-/tmp/pob-bisect.log}"
GLOBAL="${POB_ROOT}/src/Data/Global.lua"
HOST="${POB_ROOT}/runtime-macos/Path of Building-PoE2"
LAUNCH="${POB_ROOT}/src/Launch.lua"

if [[ ! -f "$GLOBAL" ]]; then
  echo "Missing $GLOBAL" >&2
  exit 1
fi

ENGINE_REPO="$(cd "$(dirname "$0")/../.." && pwd)"
echo "Syncing engine sources..."
cp "$ENGINE_REPO"/ui_api.cpp "$ENGINE_BUILD"/
ninja -C "$ENGINE_BUILD/build"
cmake --install "$ENGINE_BUILD/build" --prefix "$POB_ROOT/runtime-macos"

TOTAL=$(wc -l < "$GLOBAL" | tr -d ' ')
low=0
high="$TOTAL"
export POB_MAC_BISECT_GLOBAL=1

probe() {
  local end="$1"
  : >"$BISECT_LOG"
  if python3 - <<PY >>"$BISECT_LOG" 2>&1
import os, subprocess, sys
env = os.environ.copy()
env["POB_MAC_BISECT_GLOBAL"] = "1"
env["POB_MAC_BISECT_EXIT"] = "1"
env["POB_MAC_BISECT_END"] = "$end"
try:
    rc = subprocess.run(["$HOST", "$LAUNCH"], env=env, timeout=45).returncode
except subprocess.TimeoutExpired:
    rc = 124
sys.exit(rc)
PY
  then
    return 0
  fi
  local rc=$?
  # Syntax-only failure from snapping prefix: treat as OK boundary for search.
  if grep -q "BISECT: load lines" "$BISECT_LOG" && ! grep -q "macOS BISECT: Global.lua slice OK" "$BISECT_LOG"; then
    return 0
  fi
  return "$rc"
}

echo "Global.lua has $TOTAL lines. Probing prefixes 1..N (one process per probe)."
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
echo "  POB_MAC_BISECT_GLOBAL=1 POB_MAC_BISECT_LINES=$((low + 1))-$high lldb -- ./runtime-macos/\"Path of Building-PoE2\" ./src/Launch.lua"
