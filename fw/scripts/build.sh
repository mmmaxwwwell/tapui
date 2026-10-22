#!/usr/bin/env bash
# Build the firmware inside `nix develop .#fw`.
#   fw/scripts/build.sh                    XIAO nRF52840 (xiao_ble)
#   fw/scripts/build.sh <board> [west build args...]
# TAPUI_WEST_WORKSPACE points at another Zephyr v4.5 workspace instead of
# fetching one (e.g. fieldpad's fw/.west-workspace).
set -euo pipefail
fw="$(cd "$(dirname "$0")/.." && pwd)"
board="${1:-xiao_ble}"; shift || true
ws="${TAPUI_WEST_WORKSPACE:-$fw/.west-workspace}"
[ -d "$ws/zephyr" ] || { echo "no workspace at $ws: run fw/scripts/west-init.sh" >&2; exit 1; }
export ZEPHYR_BASE="$ws/zephyr"
cd "$ws"
west build -p auto -b "$board" -d "$fw/build/${board//\//_}" "$fw" "$@"
