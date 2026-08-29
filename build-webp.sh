#!/usr/bin/env bash
# build-webp.sh — cross-compile libwebp (decode only) for Android API 9 / armeabi.
# Produces: thirdparty/libwebp/build-android/libwebp.a
#
# Only the DECODER is needed (WebPDecodeRGBA); we disable encoder/mux/demux to
# keep the static lib tiny and avoid pulling zlib (encoding side). Decoding
# animated/extended formats is off — search covers are plain single-frame WebP.
#
# This script is driven by CI (GitHub Actions runner has network + the NDK).
# Locally it requires ANDROID_NDK_HOME r16b; run after `git submodule update`.
set -euo pipefail

cd "$(dirname "$0")"
NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"

LINK_API=16
SYSROOT="$NDK/platforms/android-$LINK_API/arch-arm"
UNIFIED_INC="$NDK/sysroot/usr/include"
UNIFIED_INC_ARCH="$NDK/sysroot/usr/include/arm-linux-androideabi"
TC="$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi"

WEBP_DIR=thirdparty/libwebp
OUT="$WEBP_DIR/build-android"
mkdir -p "$OUT"

echo "[webp] configuring libwebp for armeabi (decode only) ..."
# libwebp ships autotools; configure with the cross toolchain.
# --host selects the cross target; CC/sysroot point at the NDK r16b gcc.
( cd "$WEBP_DIR" && \
  CC="$TC-gcc --sysroot=$SYSROOT -isystem $UNIFIED_INC -isystem $UNIFIED_INC_ARCH" \
  AR="$TC-ar" \
  RANLIB="$TC-ranlib" \
  ./configure \
    --host=arm-linux-androideabi \
    --prefix="$PWD/$OUT" \
    --enable-static --disable-shared \
    --disable-libwebpdemux --disable-libwebpmux \
    --disable-libwebpextras \
    --disable-neon \
    --disable-sse2 --disable-sse4.1 --disable-avx2 \
    --disable-threading \
    --disable-png --disable-jpeg --disable-tiff --disable-gif \
    --disable-wic --disable-thor --disable-vp8-decoder --disable-vp8l-decoder 2>&1 | tail -20 || \
  { echo "[webp] configure failed"; exit 1; } )

echo "[webp] building ..."
( cd "$WEBP_DIR" && make -j4 2>&1 | tail -20 )

# Collect just the decoder .a we need.
if [ -f "$WEBP_DIR/src/.libs/libwebpdecoder.a" ]; then
    cp "$WEBP_DIR/src/.libs/libwebpdecoder.a" "$OUT/libwebp.a"
elif [ -f "$WEBP_DIR/src/libwebpdecoder.a" ]; then
    cp "$WEBP_DIR/src/libwebpdecoder.a" "$OUT/libwebp.a"
else
    echo "[webp] ERROR: libwebpdecoder.a not produced"; exit 1
fi

echo "[webp] OK -> $OUT/libwebp.a ($(wc -c < "$OUT/libwebp.a") bytes)"
file "$OUT/libwebp.a" 2>/dev/null || true
