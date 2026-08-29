#!/usr/bin/env bash
# build.sh — local build + optional Android cross-compile.
#
# Stages:
#   ./build.sh            Build the host binary ./wnacg with gcc and run parser tests.
#   ./build.sh test       Build (if needed) and run the parser unit tests only.
#   ./build.sh android    Cross-compile for Android (needs $ANDROID_NDK_HOME) and
#                         package the APK into android/app/build/outputs/wnacg.apk
#
# The host build uses the SAME C sources and the SAME BearSSL tree as the Android
# build, so "it works on my machine" means the same parsing/TLS logic that ships
# on the phone. The only differences are the toolchain and the DEFAULT_API_DOMAIN
# (identical) — there is no separate code path for Android.
set -euo pipefail

cd "$(dirname "$0")"

BS_DIR=thirdparty/bearssl-0.6
BS_LIB="$BS_DIR/build/libbearssl.a"
BS_INC="$BS_DIR/inc"

DOMAIN='www.wn09.shop'
CFLAGS="-O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE -DDEFAULT_API_DOMAIN=\"$DOMAIN\""

build_host() {
    if [ ! -f "$BS_LIB" ]; then
        echo "[build] compiling BearSSL (host) ..."
        (cd "$BS_DIR" && make CC="${CC:-gcc}")
    fi
    echo "[build] compiling ./wnacg (host, gcc) ..."
    # NOTE: libwebp is NOT linked on the host build. webp_bmp.c compiles out the
    # WebP->BMP path under #ifndef __ANDROID__, so the host binary is a plain
    # pass-through for covers. The Android build (build-android.sh) links libwebp.
    ${CC:-gcc} $CFLAGS -I"$BS_INC" \
        src/net.c src/tls.c src/html.c src/wnacg.c src/webp_bmp.c \
        -o wnacg "$BS_LIB"
    echo "[build] host binary ready: ./wnacg"
}

run_tests() {
    build_host
    echo "[test] parser unit tests ..."
    gcc -O0 -std=c99 -D_GNU_SOURCE -DDEFAULT_API_DOMAIN="\"$DOMAIN\"" \
        -Isrc -I"$BS_INC" tests/parse_test.c src/html.c -o tests/parse_test
    ./tests/parse_test
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
