#!/bin/sh
# Steam Frame launcher for Halo: Combat Evolved (OpenCE). The game data (maps/)
# lives in ~/.local/share/halo-ce, with the settings (config.toml) and saves.
cd "$(dirname "$0")" || exit 1
chmod +x ./halo 2>/dev/null
export HALO_DATA_ROOT="${HALO_DATA_ROOT:-$HOME/.local/share/halo-ce}"
mkdir -p "$HALO_DATA_ROOT"
# A disc image dropped next to this script or into the data folder: copy its
# maps out once (the port's own extractor, xiso.py beside this script).
if [ ! -f "$HALO_DATA_ROOT/maps/ui.map" ]; then
    for iso in ./*.iso ./*.xiso "$HALO_DATA_ROOT"/*.iso "$HALO_DATA_ROOT"/*.xiso; do
        [ -f "$iso" ] || continue
        python3 ./extract_maps.py "$iso" "$HALO_DATA_ROOT" >> "$HALO_DATA_ROOT/launch.log" 2>&1 && break
    done
fi
# Steam's virtual gamepad is an Xbox 360 pad, but Steam tells SDL3 it is the
# "Steam Frame Controllers" (28de:11e0), which SDL3 has no mapping for: most
# buttons and the sticks then read wrong. Without the hint it is a 360 pad.
unset SteamVirtualGamepadInfo
export LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
{ echo "== $(date)"; } >> "$HALO_DATA_ROOT/launch.log"
exec ./halo "$@" >> "$HALO_DATA_ROOT/launch.log" 2>&1
