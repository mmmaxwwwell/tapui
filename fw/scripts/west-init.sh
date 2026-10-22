#!/usr/bin/env bash
# Fetch Zephyr into fw/.west-workspace (gitignored). west wants its manifest
# inside the workspace, so the workspace gets a symlink fw -> .. and a
# hand-written .west/config.  Run inside `nix develop .#fw`.
set -euo pipefail
fw="$(cd "$(dirname "$0")/.." && pwd)"
ws="$fw/.west-workspace"
mkdir -p "$ws/.west"
ln -sfn .. "$ws/fw"
cat > "$ws/.west/config" <<CFG
[manifest]
path = fw
file = west.yml

[zephyr]
base = zephyr
CFG
cd "$ws"
west update --narrow --fetch-opt=--depth=1
echo "workspace ready: $ws"
