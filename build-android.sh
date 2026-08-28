#!/usr/bin/env bash
# build-android.sh — cross-compile the wnacg native binary for Android (API 9 / armeabi).
#
# Produces: android/app/src/main/assets/wnacg  (a single ARMv5TE executable).
#
# Why armeabi (ARMv5TE) and not armeabi-v7a: a v5 binary runs on every ARM
# Android device ever shipped (v5, v7, v8 in 32-bit mode), so one binary covers
# the widest range of real Gingerbread hardware. It is slightly slower than v7a
# but correctness > speed for a downloader.
#
# Toolchain: NDK r16b (GCC 4.9 prebuilt). The app manifest targets minSdk 9
# (Gingerbread); we LINK against the lowest platform dir the r16b NDK actually
# ships (android-16) because r16b dropped the android-9 platform directory. The
# resulting binary only uses baseline bionic symbols (malloc/free/strlen/recv/
# send/socket/...) that exist on 2.3, so it runs fine on a real Gingerbread
# device. The C sources are identical to the host build — no per-platform fork.
#
# NOTE: Android bionic has no static libc (libc is shared and present on every
# device), so the binary is dynamically linked against bionic — still a single
# self-contained file. We pass --start-group so the linker can resolve symbols
# that BearSSL references but our app objects don't.
set -euo pipefail

cd "$(dirname "$0")"

NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"
export ANDROID_NDK_ROOT="$NDK"
# r16b only ships platforms/android-{16,18,22}; link against the lowest.
LINK_API=16
MANIFEST_MIN_SDK=9
DOMAIN='www.wn09.shop'

SYSROOT="$NDK/platforms/android-$LINK_API/arch-arm"
UNIFIED_INC="$NDK/sysroot/usr/include"
UNIFIED_INC_ARCH="$NDK/sysroot/usr/include/arm-linux-androideabi"
SYSINC="-isystem $UNIFIED_INC -isystem $UNIFIED_INC_ARCH"
TC="$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi"
BS_DIR=thirdparty/bearssl-0.6
BS_INC="$BS_DIR/inc"
BS_LIB_ANDROID="$BS_DIR/build-android/libbearssl.a"
OUT_DIR=android/app/src/main/assets
OUT_BIN="$OUT_DIR/wnacg"

echo "[android] NDK=$NDK  link API=$LINK_API  (manifest minSdk=$MANIFEST_MIN_SDK)"

# 1) Cross-compile BearSSL for ARM (static lib).
if [ ! -f "$BS_LIB_ANDROID" ]; then
    echo "[android] compiling BearSSL (arm) ..."
    (cd "$BS_DIR" && \
        make BUILD=build-android \
             CC="$TC-gcc --sysroot=$SYSROOT $SYSINC" \
             AR="$TC-ar" RANLIB="$TC-ranlib" \
             lib)
fi

# 2) Cross-compile the app (dynamic link vs bionic, runs on 2.3+).
mkdir -p "$OUT_DIR"
echo "[android] compiling wnacg (arm, armv5te) ..."
"$TC-gcc" --sysroot="$SYSROOT" $SYSINC \
    -O2 -std=c99 -D_GNU_SOURCE \
    -DDEFAULT_API_DOMAIN="\"$DOMAIN\"" \
    -I"$BS_INC" \
    src/net.c src/tls.c src/html.c src/wnacg.c \
    -o "$OUT_BIN" \
    -Wl,--start-group "$BS_LIB_ANDROID" -lc -lm -ldl -Wl,--end-group

"$TC-strip" "$OUT_BIN" 2>/dev/null || true
echo "[android] wrote $OUT_BIN ($(wc -c < "$OUT_BIN") bytes)"
file "$OUT_BIN" 2>/dev/null || true
