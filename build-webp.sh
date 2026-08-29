#!/usr/bin/env bash
# build-webp.sh — cross-compile libwebp (decode only) for Android API 9 / armeabi.
# Produces: thirdparty/libwebp/build-android/libwebp.a
#
# Only the DECODER is needed (WebPDecodeRGBA); we disable encoder/mux/demux to
# keep the static lib tiny and avoid pulling zlib (encoding side). We MUST keep
# the VP8 / VP8L (lossless) DECODERS enabled — disabling them would make the
# library unable to decode ANY WebP. Animated/extended formats are off.
#
# This script is driven by CI (GitHub Actions runner has network + the NDK).
# Source is fetched on demand via curl (verified reachable from CI).
set -euo pipefail

cd "$(dirname "$0")"
NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"

WEBP_DIR=thirdparty/libwebp
WEBP_TARBALL="https://github.com/webmproject/libwebp/archive/refs/tags/v1.3.2.tar.gz"

# --- fetch source (verified: CI can curl this URL directly) ---
if [ ! -f "$WEBP_DIR/src/dec/decode.c" ]; then
  echo "[webp] source missing, fetching libwebp v1.3.2 tarball ..."
  rm -rf "$WEBP_DIR" webp-tmp webp.tar.gz
  if curl -fSL --connect-timeout 30 --max-time 300 -o webp.tar.gz "$WEBP_TARBALL"; then
    echo "[webp] tarball downloaded: $(wc -c < webp.tar.gz) bytes"
    mkdir -p webp-tmp
    if tar xzf webp.tar.gz -C webp-tmp; then
      # top dir is libwebp-<version>; match it loosely
      src=$(ls -d webp-tmp/libwebp-* 2>/dev/null | head -1)
      if [ -n "$src" ] && [ -f "$src/src/dec/decode.c" ]; then
        mv "$src" "$WEBP_DIR"
        echo "[webp] source extracted -> $WEBP_DIR"
      else
        echo "[webp] ERROR: unexpected tarball layout:"; ls -l webp-tmp
        exit 1
      fi
    else
      echo "[webp] ERROR: tarball failed to extract"; file webp.tar.gz; exit 1
    fi
  else
    echo "[webp] ERROR: curl failed to download tarball"; exit 1
  fi
  rm -rf webp-tmp webp.tar.gz
fi

LINK_API=16
SYSROOT="$NDK/platforms/android-$LINK_API/arch-arm"
UNIFIED_INC="$NDK/sysroot/usr/include"
UNIFIED_INC_ARCH="$NDK/sysroot/usr/include/arm-linux-androideabi"
TC="$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi"

OUT="$WEBP_DIR/build-android"
mkdir -p "$OUT"

echo "[webp] configuring libwebp for armeabi (decode only) ..."
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
    --disable-wic --disable-thor 2>&1 | tail -25 || \
  { echo "[webp] configure failed"; exit 1; } )

echo "[webp] building ..."
( cd "$WEBP_DIR" && make -j4 2>&1 | tail -25 )

if [ -f "$WEBP_DIR/src/.libs/libwebpdecoder.a" ]; then
    cp "$WEBP_DIR/src/.libs/libwebpdecoder.a" "$OUT/libwebp.a"
elif [ -f "$WEBP_DIR/src/libwebpdecoder.a" ]; then
    cp "$WEBP_DIR/src/libwebpdecoder.a" "$OUT/libwebp.a"
else
    echo "[webp] ERROR: libwebpdecoder.a not produced"; exit 1
fi

echo "[webp] OK -> $OUT/libwebp.a ($(wc -c < "$OUT/libwebp.a") bytes)"
file "$OUT/libwebp.a" 2>/dev/null || true
