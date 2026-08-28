#!/usr/bin/env bash
# build-android.sh — cross-compile the wnacg native binary for Android (API 9 / armeabi).
#
# Produces: android/app/src/main/assets/wnacg  (a static ARMv5TE executable).
#
# Why armeabi (ARMv5TE) and not armeabi-v7a: a v5 binary runs on every ARM
# Android device ever shipped (v5, v7, v8 in 32-bit mode), so one binary covers
# the widest range of real Gingerbread hardware. It is slightly slower than v7a
# but correctness > speed for a downloader.
#
# Toolchain: NDK r16b (last NDK that cleanly targets API 9 with the GCC 4.9
# prebuilt and platforms/android-9). Newer NDKs dropped armeabi and raise the
# minimum API, so pin r16.1.4479499 in CI / your local install.
set -euo pipefail

cd "$(dirname "$0")"

NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"
API=9
DOMAIN='www.wn09.shop'

TC="$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi"
SYSROOT="$NDK/platforms/android-$API/arch-arm"
BS_DIR=thirdparty/bearssl-0.6
BS_INC="$BS_DIR/inc"
BS_LIB_ANDROID="$BS_DIR/build-android/libbearssl.a"
OUT_DIR=android/app/src/main/assets
OUT_BIN="$OUT_DIR/wnacg"

echo "[android] NDK=$NDK  API=$API"

# 1) Cross-compile BearSSL for ARM (static lib).
if [ ! -f "$BS_LIB_ANDROID" ]; then
    echo "[android] compiling BearSSL (arm) ..."
    (cd "$BS_DIR" && \
        make BUILD=build-android \
             CC="$TC-gcc --sysroot=$SYSROOT" \
             AR="$TC-ar" RANLIB="$TC-ranlib" \
             lib)
fi

# 2) Cross-compile the app (static against the NDK libc, no shared deps).
mkdir -p "$OUT_DIR"
echo "[android] compiling wnacg (arm, static) ..."
"$TC-gcc" --sysroot="$SYSROOT" \
    -O2 -std=c99 -D_GNU_SOURCE -static \
    -DDEFAULT_API_DOMAIN="\"$DOMAIN\"" \
    -I"$BS_INC" \
    src/net.c src/tls.c src/html.c src/wnacg.c \
    -o "$OUT_BIN" "$BS_LIB_ANDROID"

"$TC-strip" "$OUT_BIN" 2>/dev/null || true
echo "[android] wrote $OUT_BIN ($(wc -c < "$OUT_BIN") bytes)"
file "$OUT_BIN" 2>/dev/null || true
