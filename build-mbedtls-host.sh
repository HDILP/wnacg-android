#!/usr/bin/env bash
# build-mbedtls-host.sh — compile mbedTLS 3.6.2 for Host (x86_64) with make
# Produces: thirdparty/mbedtls/build-host/library/libmbed{tls,x509,crypto}.a
#
# Source layout after fetch:
#   thirdparty/mbedtls/            <- repo root (include/, library/, Makefile)
set -euo pipefail

cd "$(dirname "$0")"
MBEDTLS_DIR=thirdparty/mbedtls
MBEDTLS_TARBALL="https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v3.6.2.tar.gz"
OUT="$MBEDTLS_DIR/build-host"

if [ -f "$OUT/library/libmbedtls.a" ] && [ -f "$OUT/library/libmbedcrypto.a" ]; then
  echo "[mbedtls-host] already built: $OUT/library/libmbedtls.a"
  exit 0
fi

# Fetch source if missing (tarball extracts to mbedtls-3.6.2/ — flatten it)
if [ ! -f "$MBEDTLS_DIR/include/mbedtls/ssl.h" ]; then
  echo "[mbedtls-host] fetching mbedtls v3.6.2 tarball ..."
  rm -rf "$MBEDTLS_DIR" mbedtls-tmp mbedtls.tar.gz
  curl -fSL --connect-timeout 30 --max-time 300 -o mbedtls.tar.gz "$MBEDTLS_TARBALL"
  mkdir -p mbedtls-tmp
  tar xzf mbedtls.tar.gz -C mbedtls-tmp
  src=$(find mbedtls-tmp -mindepth 1 -maxdepth 1 -type d -name 'mbedtls-*' 2>/dev/null | head -1)
  if [ -z "$src" ] || [ ! -f "$src/include/mbedtls/ssl.h" ]; then
    echo "[mbedtls-host] ERROR: unexpected tarball layout"; ls -la mbedtls-tmp; exit 1
  fi
  mv "$src" "$MBEDTLS_DIR"
  rm -rf mbedtls-tmp mbedtls.tar.gz
fi

echo "[mbedtls-host] building static libraries with make ..."
# Copy custom config
cp src/mbedtls_user_config.h "$MBEDTLS_DIR/include/mbedtls/mbedtls_user_config.h"

# Clean any previous build artifacts
make -C "$MBEDTLS_DIR" clean >/dev/null 2>&1 || true

# Build library only (no tests, no programs)
# -DMBEDTLS_USER_CONFIG_FILE must expand to a quoted filename; the awkward
# quoting builds: -DMBEDTLS_USER_CONFIG_FILE='"mbedtls/mbedtls_user_config.h"'
CFG_DEF="-DMBEDTLS_USER_CONFIG_FILE=\\\"mbedtls/mbedtls_user_config.h\\\""
export CFLAGS="-O2 -Wall -Wextra $CFG_DEF"
make -C "$MBEDTLS_DIR/library" -j4 CC="${CC:-gcc}" AR="${AR:-ar}"

mkdir -p "$OUT/library"
cp "$MBEDTLS_DIR/library/libmbedtls.a" "$OUT/library/"
cp "$MBEDTLS_DIR/library/libmbedx509.a" "$OUT/library/"
cp "$MBEDTLS_DIR/library/libmbedcrypto.a" "$OUT/library/"

echo "[mbedtls-host] OK -> $OUT/library/libmbedtls.a"
