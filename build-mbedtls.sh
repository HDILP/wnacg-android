#!/usr/bin/env bash
# build-mbedtls.sh — cross-compile mbedTLS 3.6.2 for Android API 9 / armeabi
# Produces: thirdparty/mbedtls/build-android/library/libmbed{tls,x509,crypto}.a
#
# Source layout after fetch:
#   thirdparty/mbedtls/            <- repo root (include/, library/, Makefile)
set -euo pipefail

cd "$(dirname "$0")"
NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"

MBEDTLS_DIR=thirdparty/mbedtls
MBEDTLS_TARBALL="https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v3.6.2.tar.gz"
OUT="$MBEDTLS_DIR/build-android"

if [ -f "$OUT/library/libmbedtls.a" ] && [ -f "$OUT/library/libmbedcrypto.a" ]; then
  echo "[mbedtls-android] already built: $OUT/library/libmbedtls.a"
  exit 0
fi

# Fetch source if missing (tarball extracts to mbedtls-3.6.2/ — flatten it)
if [ ! -f "$MBEDTLS_DIR/include/mbedtls/ssl.h" ]; then
  echo "[mbedtls-android] fetching mbedtls v3.6.2 tarball ..."
  rm -rf "$MBEDTLS_DIR" mbedtls-tmp mbedtls.tar.gz
  curl -fSL --connect-timeout 30 --max-time 300 -o mbedtls.tar.gz "$MBEDTLS_TARBALL"
  mkdir -p mbedtls-tmp
  tar xzf mbedtls.tar.gz -C mbedtls-tmp
  src=$(find mbedtls-tmp -mindepth 1 -maxdepth 1 -type d -name 'mbedtls-*' 2>/dev/null | head -1)
  if [ -z "$src" ] || [ ! -f "$src/include/mbedtls/ssl.h" ]; then
    echo "[mbedtls-android] ERROR: unexpected tarball layout"; ls -la mbedtls-tmp; exit 1
  fi
  mv "$src" "$MBEDTLS_DIR"
  rm -rf mbedtls-tmp mbedtls.tar.gz
fi

LINK_API=16
SYSROOT="$NDK/platforms/android-$LINK_API/arch-arm"
UNIFIED_INC="$NDK/sysroot/usr/include"
UNIFIED_INC_ARCH="$NDK/sysroot/usr/include/arm-linux-androideabi"
SYSINC="-isystem $UNIFIED_INC -isystem $UNIFIED_INC_ARCH"
TC="$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi"

echo "[mbedtls-android] cross-compiling static libraries for ARM (API 9/16) ..."
cp src/mbedtls_user_config.h "$MBEDTLS_DIR/include/mbedtls/mbedtls_user_config.h"

make -C "$MBEDTLS_DIR" clean >/dev/null 2>&1 || true

CFLAGS="--sysroot=$SYSROOT $SYSINC -O2 -march=armv5te -DMBEDTLS_USER_CONFIG_FILE='<mbedtls/mbedtls_user_config.h>'" \
  make -C "$MBEDTLS_DIR/library" -j4 CC="$TC-gcc" AR="$TC-ar"

mkdir -p "$OUT/library"
cp "$MBEDTLS_DIR/library/libmbedtls.a" "$OUT/library/"
cp "$MBEDTLS_DIR/library/libmbedx509.a" "$OUT/library/"
cp "$MBEDTLS_DIR/library/libmbedcrypto.a" "$OUT/library/"

echo "[mbedtls-android] OK -> $OUT/library/libmbedtls.a"
