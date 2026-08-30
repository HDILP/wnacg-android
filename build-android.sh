#!/usr/bin/env bash
# build-android.sh — cross-compile the wnacg native binary for Android (API 9 / armeabi).
#
# Produces: android/app/src/main/jniLibs/armeabi/libwnacg.so
#
# Why a .so (not a bare executable in assets): Android API 29+ forbids exec()ing a
# binary from the app's own data/files dir or from assets. A native library placed
# under jniLibs/ is installed by the system into nativeLibraryDir, which is one of
# the few paths where exec() is allowed — so the same layout works on 2.3 through
# 16. The binary's main() is still a normal program entry point; we just name it
# libwnacg.so and exec it directly.
#
# Why armeabi (ARMv5TE) and not armeabi-v7a: a v5 binary runs on every ARM
# Android device ever shipped (v5, v7, v8 in 32-bit mode), so one binary covers
# the widest range of real Gingerbread hardware. It is slightly slower than v7a
# but correctness > speed for a downloader.
#
# Toolchain: NDK r16b (GCC 4.9 prebuilt). The app manifest targets minSdk 9
# (Gingerbread); we LINK against the lowest platform dir the r16b NDK actually
# ships (android-16) because r16b dropped the android-9 platform directory. The
# resulting binary only uses baseline bionic symbols (malloc/free/strlen/recv/
# send/socket/...) that exist on 2.3, so it runs fine on a real Gingerbread
# device. The C sources are identical to the host build — no per-platform fork.
#
# NOTE: Android bionic has no static libc (libc is shared and present on every
# device), so the binary is dynamically linked against bionic — still a single
# self-contained file. We pass --start-group so the linker can resolve symbols
# that BearSSL references but our app objects don't.
set -euo pipefail

cd "$(dirname "$0")"

NDK="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at an NDK r16b install}"
export ANDROID_NDK_ROOT="$NDK"
# r16b only ships platforms/android-{16,18,22}; link against the lowest.
LINK_API=16
MANIFEST_MIN_SDK=9
DOMAIN='www.wn09.shop'

SYSROOT="$NDK/platforms/android-$LINK_API/arch-arm"
UNIFIED_INC="$NDK/sysroot/usr/include"
UNIFIED_INC_ARCH="$NDK/sysroot/usr/include/arm-linux-androideabi"
SYSINC="-isystem $UNIFIED_INC -isystem $UNIFIED_INC_ARCH"
TC="$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi"
BS_DIR=thirdparty/bearssl-0.6
BS_INC="$BS_DIR/inc"
BS_LIB_ANDROID="$BS_DIR/build-android/libbearssl.a"
WEBP_INC=thirdparty/libwebp/src
WEBP_A=thirdparty/libwebp/build-android/libwebp.a
OUT_DIR=android/app/src/main/jniLibs/armeabi
OUT_LIB="$OUT_DIR/libwnacg.so"

echo "[android] NDK=$NDK  link API=$LINK_API  (manifest minSdk=$MANIFEST_MIN_SDK)"

# 1) Cross-compile BearSSL for ARM (static lib).
if [ ! -f "$BS_LIB_ANDROID" ]; then
    echo "[android] compiling BearSSL (arm) ..."
    (cd "$BS_DIR" && \
        make BUILD=build-android \
             CC="$TC-gcc --sysroot=$SYSROOT $SYSINC" \
             AR="$TC-ar" RANLIB="$TC-ranlib" \
             lib)
fi

# 1b) Cross-compile libwebp (decode only) for ARM — produces $WEBP_A.
#     build-webp.sh fetches the source on demand and uses ndk-build.
if [ ! -f "$WEBP_A" ]; then
    echo "[android] compiling libwebp (arm, decode only) ..."
    bash build-webp.sh
fi

# 2) Cross-compile the app as a PIE EXECUTABLE (not -shared!) named libwnacg.so.
#    MUST be a PIE executable (-pie -fPIE), NOT -shared: a shared object has no
#    _start entry point and CANNOT be exec()'d — that would recreate the
#    "Permission denied" crash seen when running from getFilesDir on API 29+.
#    A PIE executable is an ELF ET_DYN with a real entry point, so exec() works,
#    AND aapt2 still packs a file named lib*.so into lib/armeabi/ → the system
#    installs it in nativeLibraryDir (the one exec-allowed path on modern Android).
#
#    DUAL BINARY for Android 2.3 (API 9):
#    PIE support was added to the bionic linker in Android 4.1 (API 16), so the
#    Gingerbread linker refuses to load the PIE above (exit code 11 / SIGSEGV).
#    Old systems (API 9–15) have NO exec-path restrictions, so we ALSO build a
#    classic non-PIE ET_EXEC and ship it in assets/. The Java shell picks it up
#    on SDK_INT < 16, extracts it to filesDir and execs it there. Modern
#    Android (16+) keeps using the nativeLibraryDir PIE.
mkdir -p "$OUT_DIR" android/app/src/main/assets
echo "[android] compiling wnacg-legacy (ARMv5TE, non-PIE ET_EXEC) for API 9-15 ..."
"$TC-gcc" --sysroot="$SYSROOT" $SYSINC \
    -O2 -std=c99 \
    -DDEFAULT_API_DOMAIN="\"$DOMAIN\"" \
    -I"$BS_INC" -I"$WEBP_INC" \
    src/net.c src/tls.c src/html.c src/wnacg.c src/webp_bmp.c src/png_write.c src/img_host.c \
    -o android/app/src/main/assets/wnacg-legacy \
    -Wl,--start-group "$BS_LIB_ANDROID" "$WEBP_A" -lc -lm -ldl -lz -Wl,--end-group
"$TC-strip" android/app/src/main/assets/wnacg-legacy 2>/dev/null || true
echo "[android] wrote android/app/src/main/assets/wnacg-legacy ($(wc -c < android/app/src/main/assets/wnacg-legacy) bytes)"

echo "[android] compiling wnacg (arm, armv5te, PIE exe named .so) ..."
"$TC-gcc" --sysroot="$SYSROOT" $SYSINC \
    -O2 -std=c99 -fPIE \
    -DDEFAULT_API_DOMAIN="\"$DOMAIN\"" \
    -I"$BS_INC" -I"$WEBP_INC" \
    src/net.c src/tls.c src/html.c src/wnacg.c src/webp_bmp.c src/png_write.c src/img_host.c \
    -o "$OUT_LIB" \
    -pie -fPIE \
    -Wl,--start-group "$BS_LIB_ANDROID" "$WEBP_A" -lc -lm -ldl -lz -Wl,--end-group

"$TC-strip" "$OUT_LIB" 2>/dev/null || true
echo "[android] wrote $OUT_LIB ($(wc -c < "$OUT_LIB") bytes)"
file "$OUT_LIB" 2>/dev/null || true
file android/app/src/main/assets/wnacg-legacy 2>/dev/null || true
