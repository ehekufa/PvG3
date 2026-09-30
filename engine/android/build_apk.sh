#!/bin/sh
# Собирает APK движка ОГОРОД (NativeActivity + EGL) из исходников репозитория.
#
#   engine/android/build_apk.sh            # arm64 + arm32, подпись тестовым ключом
#   ANDROID_NDK_ROOT=... BUILD_TOOLS=... engine/android/build_apk.sh
#
# Нужны Android SDK (platforms/android-34) и NDK. GitHub Actions уже содержит
# оба; локально укажи ANDROID_HOME и ANDROID_NDK_ROOT. Собирается ровно то же,
# что лежит в CI: тот же Makefile-подход, те же флаги, тот же ключ.
set -eu

ROOT=$(CD=$(dirname "$0")/../..; cd "$CD" && pwd)
SDK=${ANDROID_HOME:-}
NDK=${ANDROID_NDK_ROOT:-}
BT=${BUILD_TOOLS:-}

if [ -z "$NDK" ] && [ -n "$SDK" ]; then
    NDK=$(ls -d "$SDK"/ndk/* 2>/dev/null | head -1 || true)
fi
if [ -z "$SDK" ] || [ ! -f "$SDK/platforms/android-34/android.jar" ]; then
    echo "::error::нужен ANDROID_HOME с platforms/android-34"; exit 1;
fi
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "::error::нужен ANDROID_NDK_ROOT (или SDK/ndk/*)"; exit 1;
fi
if [ -z "$BT" ]; then
    BT="$SDK/build-tools/34.0.0"
fi
for tool in aapt zipalign apksigner; do
    [ -x "$BT/$tool" ] || { echo "::error::нет $BT/$tool"; exit 1; }
done
[ -f "$ROOT/engine/android/AndroidManifest.xml" ] || { echo "::error::нет манифеста"; exit 1; }

GLUE_HDR=$(find "$NDK" -name 'android_native_app_glue.h' 2>/dev/null | head -1)
GLUE_SRC=$(find "$NDK" -name 'android_native_app_glue.c' 2>/dev/null | head -1)
[ -n "$GLUE_HDR" ] && [ -n "$GLUE_SRC" ] || { echo "::error::нет native_app_glue в NDK"; exit 1; }
GLUE_INC=$(dirname "$GLUE_HDR")
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"

cd "$ROOT"
echo "==> упаковываем игры в заголовок"
python3 tools/pack_project.py

ENGINE_SRC="engine/og_common.c engine/og_vm.c engine/og_image.c engine/og_node.c \
engine/og_runtime.c engine/og_bind.c engine/og_scene.c engine/og_vfs.c \
src/font.c engine/android/og_android.c"
CFLAGS="-O3 -s -fPIC -std=c11 -Wall -Wextra -Werror -Iengine -Iengine/android -Isrc -I$GLUE_INC"
# native_app_glue — код NDK, его предупреждениями не управляем
GLUE_CFLAGS="-O3 -fPIC -std=c11 -I$GLUE_INC"
LIBS="-landroid -lEGL -lGLESv2 -llog -lm"

mkdir -p staging_ogorod/lib/arm64-v8a staging_ogorod/lib/armeabi-v7a
rm -rf build_ogorod   # объектники — вне staging, иначе попадут в APK

# $1 — префикс компилятора NDK (arm64-v8a или armeabi-v7a), $2 — каталог ABI
build_abi() {
    CC_ABI="$TC/$1-clang"
    OBJ="build_ogorod/$2"
    mkdir -p "$OBJ"
    echo "==> $2: native_app_glue (код NDK)"
    "$CC_ABI" $GLUE_CFLAGS -c "$GLUE_SRC" -o "$OBJ/glue.o"
    for src in $ENGINE_SRC; do
        obj="$OBJ/$(echo "$src" | tr / _ | sed 's/\.c$/.o/')"
        echo "==> $2: $src"
        "$CC_ABI" $CFLAGS -c "$src" -o "$obj"
    done
    echo "==> $2: линковка"
    "$CC_ABI" -shared -s $OBJ/*.o $LIBS -o "staging_ogorod/lib/$2/libogorod.so"
}

build_abi aarch64-linux-android29 arm64-v8a
build_abi armv7a-linux-androideabi armeabi-v7a

echo "==> лицензии и ассеты"
mkdir -p staging_ogorod/assets/licenses staging_ogorod/assets/projects
cp assets/fonts/OFL.txt staging_ogorod/assets/licenses/PT_Sans-OFL.txt
cp assets/licenses/stb_truetype-MIT.txt staging_ogorod/assets/licenses/stb_truetype-MIT.txt
cp LICENSE staging_ogorod/assets/licenses/PvG3-MIT.txt
# исходники игр кладём рядом с APK: их можно распаковать и править
cp -r projects/oborona projects/kirpichi staging_ogorod/assets/projects/

echo "==> пакет"
"$BT/aapt" package -f -0 so -M engine/android/AndroidManifest.xml \
    -I "$SDK/platforms/android-34/android.jar" -F unsigned_ogorod.apk ./staging_ogorod/
"$BT/zipalign" -f 4 unsigned_ogorod.apk aligned_ogorod.apk

keytool -genkeypair -v -keystore ogorod.keystore -storepass android -keypass android \
    -alias ogorod -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Ogorod" \
    >/dev/null 2>&1 || true
"$BT/apksigner" sign --ks ogorod.keystore --ks-pass pass:android \
    --out OboronOgorod.apk aligned_ogorod.apk
"$BT/apksigner" verify --verbose OboronOgorod.apk

for want in lib/arm64-v8a/libogorod.so lib/armeabi-v7a/libogorod.so \
            assets/projects/oborona/scripts/game.og \
            assets/projects/kirpichi/scripts/game.og; do
    unzip -l OboronOgorod.apk | grep -q "$want" || {
        echo "::error::в APK нет $want"; exit 1; }
done
"$BT/aapt" dump badging OboronOgorod.apk | head -5

echo
echo "Готово: $ROOT/OboronOgorod.apk"
ls -la "$ROOT/OboronOgorod.apk"
