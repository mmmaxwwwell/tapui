#!/usr/bin/env bash
# Unit tests for src/tapui's pure parts on native_sim. Same workspace rules
# as build.sh.
set -euo pipefail
fw="$(cd "$(dirname "$0")/.." && pwd)"
ws="${TAPUI_WEST_WORKSPACE:-$fw/.west-workspace}"
export ZEPHYR_BASE="$ws/zephyr"
cd "$ws"
west build -p auto -b native_sim/native/64 -d "$fw/build/tests_core" "$fw/tests/core"
"$fw/build/tests_core/zephyr/zephyr.exe"
