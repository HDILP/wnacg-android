#!/usr/bin/env bash
# build.sh — local build + optional Android cross-compile.
# See README for full documentation.
set -euo pipefail

cd "$(dirname "$0")"

MBEDTLS_DIR=thirdparty/mbedtls
MBEDTLS_INC="$MBEDTLS_DIR/include"
MBEDTLS_LIBS="$MBEDTLS_DIR/build-host/library/libmbedtls.a $MBEDTLS_DIR/build-host/library/libmbedx509.a $MBEDTLS_DIR/build-host/library/libmbedcrypto.a"

DOMAIN='www.wn10.shop'
# The -DMBEDTLS_USER_CONFIG_FILE macro must expand to a quoted string that the
# preprocessor can use as #include "mbedtls/mbedtls_user_config.h".
# The shell strips one layer of quoting; gcc sees a macro whose value is
# '"mbedtls/mbedtls_user_config.h"' (with the quotes) — exactly what
# build_info.h needs for #include MBEDTLS_USER_CONFIG_FILE.
CFG_DEF="-DMBEDTLS_USER_CONFIG_FILE=\"mbedtls/mbedtls_user_config.h\""
CFLAGS="-O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE -DDEFAULT_API_DOMAIN=\"$DOMAIN\" $CFG_DEF"

# Test binaries need the same DEFAULT_API_DOMAIN macro but the quoting goes
# through one more shell layer, so the backslashes multiply.
TEST_DEF="-DDEFAULT_API_DOMAIN=\\\"\\\\\\\"$DOMAIN\\\\\\\"\\\""

build_host() {
    bash build-mbedtls-host.sh
    echo "[build] compiling ./wnacg (host, gcc) ..."
    # NOTE: libwebp is NOT linked on the host build. webp_bmp.c compiles out the
    # WebP->PNG path under #ifndef __ANDROID__, so the host binary is a plain
    # pass-through for covers. The Android build (build-android.sh) links libwebp.
    ${CC:-gcc} $CFLAGS -I"$MBEDTLS_INC" -Isrc \
        src/net.c src/tls.c src/html.c src/zip.c src/wnacg.c src/webp_bmp.c src/img_host.c \
        -o wnacg $MBEDTLS_LIBS
    echo "[build] host binary ready: ./wnacg"
}

run_tests() {
    build_host
    echo "[test] parser unit tests ..."
    gcc -O0 -std=c99 -D_GNU_SOURCE $TEST_DEF \
        -Isrc -I"$MBEDTLS_INC" tests/parse_test.c src/html.c src/img_host.c -o tests/parse_test
    ./tests/parse_test

    echo "[test] zip download-index parser unit tests ..."
    gcc -O0 -std=c99 -D_GNU_SOURCE \
        -DDEFAULT_API_DOMAIN='"www.wn10.shop"' \
        -DMBEDTLS_USER_CONFIG_FILE='"mbedtls/mbedtls_user_config.h"' \
        -Isrc -I"$MBEDTLS_INC" tests/zip_parse_test.c src/zip.c src/net.c src/tls.c src/img_host.c src/html.c \
        $MBEDTLS_LIBS -o tests/zip_parse_test
    ./tests/zip_parse_test

    echo "[test] WebP->PNG cover round-trip ..."
    bash build-webp-host.sh
    WEBP_LIBDIR=thirdparty/libwebp/build-host
    WEBP_INC=thirdparty/libwebp/src
    # link every static lib cmake produced (libwebp.a + libsharpyuv.a, etc.)
    WEBP_LIBS=$(ls "$WEBP_LIBDIR"/*.a 2>/dev/null)
    gcc -O2 -std=c99 -Isrc -I"$WEBP_INC" \
        tests/webp_roundtrip.c src/png_write.c -o tests/webp_roundtrip \
        -Wl,--start-group $WEBP_LIBS -Wl,--end-group -lm -lz
    ./tests/webp_roundtrip
}

cmd="${1:-}"

case "$cmd" in
    ""|host)
        build_host
        ;;
    test)
        run_tests
        ;;
    android)
        if [ -z "${ANDROID_NDK_HOME:-}" ]; then
            echo "[build] ANDROID_NDK_HOME not set; cannot cross-compile." >&2
            echo "[build] Install the NDK (r16b) and export ANDROID_NDK_HOME, then re-run." >&2
            exit 1
        fi
        ./build-android.sh
        ./packapk.sh
        ;;
    *)
        echo "usage: $0 [test|android]" >&2
        exit 2
        ;;
esac
