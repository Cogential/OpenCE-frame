#!/bin/sh
# Builds the Steam Frame (ARM64 Linux) host, `halo`, from the Android port's host
# (port/android/host) with port/frame's replacements. The game itself is the
# Android build's image: run `ninja build/android/halo_guest.elf` first.
#
# Env: CC (a clang for aarch64-linux-gnu), SYSROOT (an arm64 glibc sysroot with
# the GLES/EGL headers), SDL3_PREFIX (an ARM64 SDL3 install), OUT (default
# build/frame).
set -e
cd "$(dirname "$0")/../.."
: "${CC:=clang}"
: "${OUT:=build/frame}"
TARGET="--target=aarch64-linux-gnu ${SYSROOT:+--sysroot=$SYSROOT}"
CFLAGS="-O2 -g -fPIC -Wall -Wno-unused-function -D_GNU_SOURCE -DHALO_FRAME -include port/frame/frame_shim.h
	-Iport/frame -Iport/frame/shim -Iport/android/include -Iport/android/host -I$SDL3_PREFIX/include
	-Iport/linux/src -Iport/third_party/tomlc17"
UPNP="-Iport/third_party/miniupnpc/include -Iport/third_party/miniupnpc/src -DMINIUPNP_STATICLIB
	-DMINIUPNPC_SET_SOCKET_TIMEOUT -DMINIUPNPC_GET_SRC_ADDR -D_BSD_SOURCE -D_DEFAULT_SOURCE"
[ -f build/android/host/host_import_table.c ] || { echo "Build build/android/halo_guest.elf first"; exit 1; }
mkdir -p "$OUT/obj"
OBJECTS=""
compile() { # source object flags
	$CC $TARGET $CFLAGS $3 -c "$1" -o "$OUT/obj/$2.o"
	OBJECTS="$OBJECTS $OUT/obj/$2.o"
}
for f in port/android/host/*.c; do
	[ "$(basename "$f")" = host_main.c ] && continue
	compile "$f" "$(basename "$f" .c)"
done
compile port/frame/host_main_linux.c host_main_linux
compile port/frame/frame_gyro.c frame_gyro
compile build/android/host/host_import_table.c host_import_table
compile port/linux/src/posix_files.c posix_files
compile port/linux/src/posix_net.c posix_net
compile port/third_party/tomlc17/tomlc17.c tomlc17
compile port/linux/src/posix_upnp.c posix_upnp "$UPNP"
for f in port/third_party/miniupnpc/src/*.c; do
	compile "$f" "miniupnpc_$(basename "$f" .c)" "$UPNP -w"
done
$CC $TARGET -fuse-ld=lld -o "$OUT/halo" $OBJECTS -L"$SDL3_PREFIX/lib" -lSDL3 -lGLESv2 -lEGL -lm -ldl -lpthread \
	-Wl,-rpath,'$ORIGIN/lib' -Wl,--no-undefined
echo "built $OUT/halo"
