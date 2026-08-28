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
#
# We build a STANDALONE toolchain via the NDK helper script. GCC 4.9's
# --sysroot handling against the unified NDK layout is fragile (it fails to
# locate string.h); the standalone toolchain bakes in a correct, self-contained
# sysroot, so plain `$TC-gcc` just works without --sysroot.
set -euo pipefail

cd "$(dirname "$0")"

NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"
API=9
DOMAIN='www.wn09.shop'

# Standalone toolchain (baked-in sysroot for API 9 / armeabi).
STANDALONE="${ANDROID_STANDALONE_TOOLCHAIN:-/opt/android-toolchain-api$API}"
if [ ! -x "$STANDALONE/bin/arm-linux-androideabi-gcc" ]; then
    echo "[android] building standalone toolchain (API $API, armeabi) ..."
    "$NDK/build/tools/make-standalone-toolchain.sh" \
        --platform="android-$API" \
        --arch=arm \
        --install-dir="$STANDALONE"
fi

TC="$STANDALONE/bin/arm-linux-androideabi"
SYSROOT="$STANDALONE/sysroot"
BS_DIR=thirdparty/bearssl-0.6
BS_INC="$BS_DIR/inc"
BS_LIB_ANDROID="$BS_DIR/build-android/libbearssl.a"
OUT_DIR=android/app/src/main/assets
OUT_BIN="$OUT_DIR/wnacg"

echo "[android] toolchain=$STANDALONE  API=$API"

# 1) Cross-compile BearSSL for ARM (static lib).
if [ ! -f "$BS_LIB_ANDROID" ]; then
    echo "[android] compiling BearSSL (arm) ..."
    (cd "$BS_DIR" && \
        make BUILD=build-android \
             CC="$TC-gcc" \
             AR="$TC-ar" RANLIB="$TC-ranlib" \
             lib)
fi

# 2) Cross-compile the app (static against the NDK libc, no shared deps).
mkdir -p "$OUT_DIR"
echo "[android] compiling wnacg (arm, static) ..."
"$TC-gcc" \
    -O2 -std=c99 -D_GNU_SOURCE -static \
    -DDEFAULT_API_DOMAIN="\"$DOMAIN\"" \
    -I"$BS_INC" \
    src/net.c src/tls.c src/html.c src/wnacg.c \
    -o "$OUT_BIN" "$BS_LIB_ANDROID"

"$TC-strip" "$OUT_BIN" 2>/dev/null || true
echo "[android] wrote $OUT_BIN ($(wc -c < "$OUT_BIN") bytes)"
file "$OUT_BIN" 2>/dev/null || true
