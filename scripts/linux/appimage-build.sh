#!/bin/bash
# Runs inside the packaging/linux/Containerfile container, from the checkout root
# (scripts/build-linux.sh --appimage starts it): builds the translator,
# translates the game if generated/ is missing, builds MH3 and packages
# MH3_APPIMAGE_OUT. Inputs (environment): MH3_BUILD_DIR, MH3_APPIMAGE_OUT,
# MH3_APPIMAGE_VERSION, MH3_CMAKE_ARGS, MH3_RETRANSLATE (0/1), MH3_JOBS.
set -euo pipefail
cd "$(dirname "$0")/../.."
root="$PWD"
: "${MH3_BUILD_DIR:=build/appimage-x86_64}" "${MH3_APPIMAGE_OUT:=dist/MH3-x86_64.AppImage}"
: "${MH3_APPIMAGE_VERSION:=unknown}" "${MH3_CMAKE_ARGS:=}" "${MH3_RETRANSLATE:=0}"
: "${MH3_JOBS:=$(nproc)}"
export HOME="$root/build/.home" DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
mkdir -p "$HOME"
DOTNET_BIN="$(command -v dotnet)"
export DOTNET_BIN

python3 -c 'import sys; assert sys.version_info >= (3, 11), sys.version' ||
  { echo "python3 in the container is older than 3.11"; exit 1; }

echo "==> Building the translator (.NET)"
dotnet build -c Release toolchain/translator/src/Translator.Cli 2>&1 | tail -n 3

if [[ $MH3_RETRANSLATE == 1 || ! -f generated/translation.json || ! -f generated/build_shards/shards.cmake ]]; then
  echo "==> Translating the game (PowerPC -> C++, several minutes)"
  TRANSLATE_JOBS="$MH3_JOBS" scripts/translate.sh
fi

echo "==> Building MH3 ($MH3_BUILD_DIR; this is the long part)"
# libstdc++/libgcc linked in, their symbols kept private: the host's Mesa keeps
# its own (newer) libstdc++, and nothing older shadows it.
export LDFLAGS="-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL"
if ! MH3_ARCH=x86_64 MH3_CLANG=clang MH3_BUILD_DIR="$MH3_BUILD_DIR" BUILD_JOBS="$MH3_JOBS" \
     MH3_EXTRA_CMAKE_ARGS="$MH3_CMAKE_ARGS -DPython3_EXECUTABLE=/usr/bin/python3.11" scripts/build-aurora.sh; then
  tail -n 25 logs/aurora-configure.log 2>/dev/null | sed 's/^/    configure| /' || true
  tail -n 40 logs/aurora-build.log 2>/dev/null | sed 's/^/    build| /' || true
  exit 1
fi

echo "==> Packaging the AppImage"
pkg="$root/build/appimage"
tools="$pkg/tools"
mkdir -p "$tools"
for t in linuxdeploy:linuxdeploy/linuxdeploy appimagetool:AppImage/appimagetool; do
  name="${t%%:*}" repo="${t#*:}"
  if [[ ! -x $tools/$name ]]; then
    curl -fL --retry 3 -o "$tools/$name" "https://github.com/$repo/releases/download/continuous/$name-x86_64.AppImage"
    chmod +x "$tools/$name"
  fi
done
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
appdir="$pkg/AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin"
cp "$MH3_BUILD_DIR/MH3" "$appdir/usr/bin/MH3"
cp runtime/sce_sys/icon0.png "$pkg/mh3-wii.png"
# Bundles what MH3 links that a host may lack (OpenSSL). Graphics, audio,
# X11/Wayland, input and glibc come from the host, which keeps Mesa and the
# Steam Deck's drivers in charge.
"$tools/linuxdeploy" --appdir "$appdir" --executable "$appdir/usr/bin/MH3" \
  --desktop-file packaging/linux/mh3-wii.desktop --icon-file "$pkg/mh3-wii.png" \
  --custom-apprun packaging/linux/AppRun \
  --exclude-library 'libGL*' --exclude-library 'libEGL*' --exclude-library 'libGLES*' \
  --exclude-library 'libGLX*' --exclude-library 'libGLdispatch*' --exclude-library 'libOpenGL*' \
  --exclude-library 'libdrm*' --exclude-library 'libgbm*' --exclude-library 'libvulkan*' \
  --exclude-library 'libasound*' --exclude-library 'libpulse*' --exclude-library 'libwayland*' \
  --exclude-library 'libX*' --exclude-library 'libxcb*' --exclude-library 'libdecor*' \
  --exclude-library 'libudev*' --exclude-library 'libdbus*' --exclude-library 'libusb*'
mkdir -p "$(dirname "$MH3_APPIMAGE_OUT")"
ARCH=x86_64 VERSION="$MH3_APPIMAGE_VERSION" "$tools/appimagetool" --no-appstream "$appdir" "$MH3_APPIMAGE_OUT"
