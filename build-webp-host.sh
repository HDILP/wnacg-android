#!/usr/bin/env bash
# build-webp-host.sh — build libwebp (decode+encode) for the HOST (x86_64) so the
# round-trip unit test can run without a device. Produces:
#   thirdparty/libwebp/build-host/libwebp.a  (static, has WebPDecode/EncodeRGBA)
# Headers stay at thirdparty/libwebp/src (e.g. webp/decode.h, webp/encode.h).
set -euo pipefail

cd "$(dirname "$0")"
WEBP_DIR=thirdparty/libwebp
WEBP_TARBALL="https://github.com/webmproject/libwebp/archive/refs/tags/v1.3.2.tar.gz"
OUT="$WEBP_DIR/build-host"

if [ -f "$OUT/libwebp.a" ]; then
  echo "[webp-host] already built: $OUT/libwebp.a"
  exit 0
fi

# fetch source if missing
if [ ! -f "$WEBP_DIR/src/dec/webp_dec.c" ]; then
  echo "[webp-host] fetching libwebp v1.3.2 tarball ..."
  rm -rf "$WEBP_DIR" webp-tmp webp.tar.gz
  curl -fSL --connect-timeout 30 --max-time 300 -o webp.tar.gz "$WEBP_TARBALL"
  mkdir -p webp-tmp
  tar xzf webp.tar.gz -C webp-tmp
  src=$(find webp-tmp -maxdepth 1 -type d -name 'libwebp-*' 2>/dev/null | head -1)
  mv "$src" "$WEBP_DIR"
  rm -rf webp-tmp webp.tar.gz
fi

# build with cmake (present on ubuntu-latest runners)
command -v cmake >/dev/null 2>&1 || { echo "[webp-host] ERROR: cmake not found"; exit 1; }
rm -rf "$OUT"
cmake -S "$WEBP_DIR" -B "$OUT" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DWEBP_BUILD_LIBWEBPDECODER=OFF \
  -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
  -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF \
  -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF \
  -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF >/dev/null
cmake --build "$OUT" -j4

# locate the produced static lib (cmake names it libwebp.a)
A=$(find "$OUT" -name 'libwebp.a' | head -1)
if [ -z "$A" ]; then echo "[webp-host] ERROR: libwebp.a not produced"; exit 1; fi
# normalize to $OUT/libwebp.a
cp "$A" "$OUT/libwebp.a"
echo "[webp-host] OK -> $OUT/libwebp.a ($(wc -c < "$OUT/libwebp.a") bytes)"
