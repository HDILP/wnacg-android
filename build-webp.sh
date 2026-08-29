#!/usr/bin/env bash
# build-webp.sh — cross-compile libwebp (decode only) for Android API 9 / armeabi.
# Produces: thirdparty/libwebp/build-android/libwebp.a
#
# Only the DECODER is needed (WebPDecodeRGBA); we disable encoder/mux/demux to
# keep the static lib tiny and avoid pulling zlib (encoding side). Decoding
# animated/extended formats is off — search covers are plain single-frame WebP.
#
# This script is driven by CI (GitHub Actions runner has network + the NDK).
# Locally it requires ANDROID_NDK_HOME r16b; run after fetching the submodule.
#
# The libwebp source is fetched on demand here (shallow clone) so the build does
# not depend on a gitlink being present in the index — CI clone of the submodule
# is attempted with retries, and fails loudly if GitHub is unreachable.
set -euo pipefail

cd "$(dirname "$0")"
NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"

WEBP_DIR=thirdparty/libwebp
WEBP_BRANCH=v1.3.2
WEBP_URL=https://github.com/webmproject/libwebp.git

# Fetch source if missing. Prefer a release tarball via curl (single HTTP GET,
# more resilient to the flaky GitHub git protocol on CI), fall back to git clone.
WEBP_TARBALL="https://github.com/webmproject/libwebp/archive/refs/tags/v1.3.2.tar.gz"
if [ ! -f "$WEBP_DIR/src/dec/decode.c" ]; then
  echo "[webp] source missing, fetching libwebp v1.3.2 ..."
  fetched=0
  # --- try tarball (up to 4 attempts) ---
  for attempt in 1 2 3 4; do
    echo "[webp] tarball attempt $attempt/4"
    rm -rf "$WEBP_DIR" webp-tmp
    if curl -fSL --retry 2 --connect-timeout 20 --max-time 180 \
         "$WEBP_TARBALL" -o webp.tar.gz 2>dl_err.txt; then
      mkdir -p webp-tmp
      if tar xzf webp.tar.gz -C webp-tmp 2>/dev/null; then
        # archive top dir is libwebp-1.3.2/
        mv webp-tmp/libwebp-1.3.2 "$WEBP_DIR" 2>/dev/null && \
          [ -f "$WEBP_DIR/src/dec/decode.c" ] && { echo "[webp] tarball OK"; fetched=1; break; }
      fi
    else
      echo "[webp] tarball download failed:"; tail -3 dl_err.txt
    fi
    sleep 3
  done
  rm -rf webp-tmp webp.tar.gz dl_err.txt
  # --- fall back to git clone (up to 3 attempts) ---
  if [ "$fetched" -ne 1 ]; then
    for attempt in 1 2 3; do
      echo "[webp] git clone attempt $attempt/3"
      rm -rf "$WEBP_DIR"
      if GIT_HTTP_LOW_SPEED_LIMIT=1000 GIT_HTTP_LOW_SPEED_TIME=30 \
         git clone --depth 1 --branch "$WEBP_BRANCH" "$WEBP_URL" "$WEBP_DIR" \
           2>clone_err.txt; then
        [ -f "$WEBP_DIR/src/dec/decode.c" ] && { echo "[webp] clone OK"; fetched=1; break; }
      else
        echo "[webp] clone failed (attempt $attempt):"; tail -5 clone_err.txt
      fi
      sleep 3
    done
    rm -f clone_err.txt
  fi
  if [ "$fetched" -ne 1 ]; then
    echo "[webp] ERROR: could not fetch libwebp source"
    exit 1
  fi
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
    --disable-wic --disable-thor --disable-vp8-decoder --disable-vp8l-decoder 2>&1 | tail -20 || \
  { echo "[webp] configure failed"; exit 1; } )

echo "[webp] building ..."
( cd "$WEBP_DIR" && make -j4 2>&1 | tail -20 )

if [ -f "$WEBP_DIR/src/.libs/libwebpdecoder.a" ]; then
    cp "$WEBP_DIR/src/.libs/libwebpdecoder.a" "$OUT/libwebp.a"
elif [ -f "$WEBP_DIR/src/libwebpdecoder.a" ]; then
    cp "$WEBP_DIR/src/libwebpdecoder.a" "$OUT/libwebp.a"
else
    echo "[webp] ERROR: libwebpdecoder.a not produced"; exit 1
fi

echo "[webp] OK -> $OUT/libwebp.a ($(wc -c < "$OUT/libwebp.a") bytes)"
file "$OUT/libwebp.a" 2>/dev/null || true
