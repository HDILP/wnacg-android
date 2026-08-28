#!/usr/bin/env bash
# packapk.sh — assemble a minimal, API-9 APK from the cross-compiled native
# binary + a thin Java shell, WITHOUT Android Gradle Plugin (AGP dropped API 9
# support long ago). We drive the SDK build-tools directly:
#   aapt2 compile/link  -> unsigned apk with resources + manifest + assets
#   javac + d8          -> classes.dex
#   zipalign + apksigner-> signed, installable apk
#
# Requires: $ANDROID_HOME (SDK root) with build-tools (aapt2/d8/apksigner/
# zipalign) and platforms;android-9 installed, plus a JDK (keytool).
set -euo pipefail

cd "$(dirname "$0")"

: "${ANDROID_HOME:?ANDROID_HOME must point at an Android SDK with build-tools + android-9}"
BT_VER="${BT_VER:-29.0.3}"
BT="$ANDROID_HOME/build-tools/$BT_VER"
PLAT="$ANDROID_HOME/platforms/android-9"

APP_ROOT=android/app/src/main
OUT=android/app/build/outputs
WORK=android/app/build/apkwork
mkdir -p "$OUT" "$WORK"

echo "[apk] using build-tools $BT_VER"

# 1) Resources -> compiled flat res archive.
echo "[apk] aapt2 compile resources ..."
"$BT/aapt2" compile --dir "$APP_ROOT/res" -o "$WORK/res.zip"

# 2) Link: manifest + resources + assets (the native binary lives in assets/).
echo "[apk] aapt2 link ..."
"$BT/aapt2" link --no-auto-version \
    -o "$WORK/app-unsigned.apk" \
    -I "$PLAT/android.jar" \
    --manifest "$APP_ROOT/AndroidManifest.xml" \
    -R "$WORK/res.zip" \
    -A "$APP_ROOT/assets" \
    --min-sdk-version 9 --target-sdk-version 9 \
    --rename-manifest-package com.wnacg.android

# 3) Java -> dex.
echo "[apk] javac + d8 ..."
rm -rf "$WORK/obj"; mkdir -p "$WORK/obj"
javac -source 1.8 -target 1.8 -cp "$PLAT/android.jar" -d "$WORK/obj" \
    "$APP_ROOT/java/com/wnacg/android/MainActivity.java"
"$BT/d8" --output "$WORK/classes.dex" $(find "$WORK/obj" -name '*.class')

# 4) Merge dex into the apk (apk is a zip).
echo "[apk] add classes.dex ..."
(cd "$WORK" && cp app-unsigned.apk app-withdex.apk && zip -q -j app-withdex.apk classes.dex)

# 5) Align + sign with a throwaway debug keystore.
echo "[apk] zipalign + apksigner ..."
keytool -genkeypair -v \
    -keystore "$WORK/debug.keystore" -alias androiddebugkey \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -storepass android -keypass android \
    -dname "CN=wnacg,O=wnacg" 2>/dev/null
"$BT/zipalign" -p 4 "$WORK/app-withdex.apk" "$WORK/app-aligned.apk"
"$BT/apksigner" sign \
    --ks "$WORK/debug.keystore" --ks-key-alias androiddebugkey \
    --ks-pass pass:android --key-pass pass:android \
    --out "$OUT/wnacg.apk" "$WORK/app-aligned.apk"

echo "[apk] verifying ..."
"$BT/apksigner" verify "$OUT/wnacg.apk" && echo "[apk] OK: $(wc -c < "$OUT/wnacg.apk") bytes -> $OUT/wnacg.apk"
