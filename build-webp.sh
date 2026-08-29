#!/usr/bin/env bash
# build-webp.sh — cross-compile libwebp (decode only) for Android API 9 / armeabi.
# Produces: thirdparty/libwebp/build-android/libwebp.a
#
# Only the DECODER is needed (WebPDecodeRGBA). We build libwebpdecoder as a
# static lib via NDK's ndk-build (libwebp ships an Android.mk). No encoder,
# no mux/demux, no zlib dependency (decoding does not need zlib).
#
# This script is driven by CI (GitHub Actions runner has network + the NDK).
set -euo pipefail

cd "$(dirname "$0")"
NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"

WEBP_DIR=thirdparty/libwebp
WEBP_TARBALL="https://github.com/webmproject/libwebp/archive/refs/tags/v1.3.2.tar.gz"

# --- fetch source (verified: CI can curl this URL directly) ---
if [ ! -f "$WEBP_DIR/src/dec/webp_dec.c" ]; then
  echo "[webp] source missing, fetching libwebp v1.3.2 tarball ..."
  rm -rf "$WEBP_DIR" webp-tmp webp.tar.gz
  if curl -fSL --connect-timeout 30 --max-time 300 -o webp.tar.gz "$WEBP_TARBALL"; then
    echo "[webp] tarball downloaded: $(wc -c < webp.tar.gz) bytes"
    mkdir -p webp-tmp
    if tar xzf webp.tar.gz -C webp-tmp; then
      src=$(find webp-tmp -maxdepth 1 -type d -name 'libwebp-*' 2>/dev/null | head -1)
      if [ -n "$src" ] && [ -f "$src/src/dec/webp_dec.c" ]; then
        mv "$src" "$WEBP_DIR"
        echo "[webp] source extracted -> $WEBP_DIR"
      else
        echo "[webp] ERROR: unexpected tarball layout"; ls -la webp-tmp; exit 1
      fi
    else
      echo "[webp] ERROR: tarball failed to extract"; file webp.tar.gz; exit 1
    fi
  else
    echo "[webp] ERROR: curl failed to download tarball"; exit 1
  fi
  rm -rf webp-tmp webp.tar.gz
fi

OUT="$WEBP_DIR/build-android"
mkdir -p "$OUT"

# --- build via ndk-build (libwebp ships Android.mk) ---
# Wrap it: a tiny jni/ project that includes libwebp's Android.mk and builds
# only the decoder static library for armeabi @ android-9.
BUILD=webp_build
rm -rf "$BUILD"
mkdir -p "$BUILD/jni"
cat > "$BUILD/jni/Application.mk" <<EOF
APP_ABI := armeabi
APP_PLATFORM := android-9
APP_STL := none
APP_OPTIM := release
EOF
cat > "$BUILD/jni/Android.mk" <<EOF
LOCAL_PATH := \$(call my-dir)
WEBP_ROOT := \$(LOCAL_PATH)/../../$WEBP_DIR
include \$(WEBP_ROOT)/android.mk
EOF

echo "[webp] ndk-build (armeabi, api9) ..."
( cd "$BUILD" && "$NDK/ndk-build" APP_ABI=armeabi APP_PLATFORM=android-9 -j4 2>&1 | tail -25 ) || \
  { echo "[webp] ndk-build failed"; exit 1; }

DEC_A="$BUILD/obj/local/armeabi/libwebpdecoder.a"
if [ -f "$DEC_A" ]; then
  cp "$DEC_A" "$OUT/libwebp.a"
else
  echo "[webp] ERROR: libwebpdecoder.a not produced"; exit 1
fi

echo "[webp] OK -> $OUT/libwebp.a ($(wc -c < "$OUT/libwebp.a") bytes)"
file "$OUT/libwebp.a" 2>/dev/null || true
