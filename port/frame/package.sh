#!/bin/sh
# Packages the Steam Frame build: port/frame/package.sh <halo> <halo_guest.elf> <libSDL3.so.0> <out.zip>
# makes Halo_CE/ (run.sh, the host, the image, SDL3) and zips it. No game data goes in.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TOP="$HERE/../.."
HOST="$1"; IMAGE="$2"; SDL="$3"; ZIP="$4"
OUT="$(dirname "$ZIP")/Halo_CE"
rm -rf "$OUT" && mkdir -p "$OUT/lib" "$OUT/licenses"
cp "$HOST" "$IMAGE" "$OUT/"
cp -L "$SDL" "$OUT/lib/libSDL3.so.0"
cp "$HERE/run.sh" "$HERE/extract_maps.py" "$HERE/README-frame.txt" "$OUT/"
cp "$TOP/port/assets/network/brokers.txt" "$OUT/"
cp "$TOP/LICENSE.md" "$OUT/licenses/OpenCE-LICENSE.md"
cp "$TOP/port/third_party/extract-xiso/LICENSE.TXT" "$OUT/licenses/extract-xiso-LICENSE.txt" 2>/dev/null || true
[ -n "$SDL3_LICENSE" ] && cp "$SDL3_LICENSE" "$OUT/licenses/SDL3-LICENSE.txt"
chmod +x "$OUT/halo" "$OUT/run.sh"
find "$OUT" \( -iname '*.map' -o -iname '*.iso' -o -iname '*.xiso' \) -delete
rm -f "$ZIP"
(cd "$(dirname "$OUT")" && zip -qry "$(basename "$ZIP")" Halo_CE)
echo "packaged $ZIP"
