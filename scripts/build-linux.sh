#!/usr/bin/env bash
# Build Monster Hunter Tri for x86-64 Linux from this checkout: a native build
# on Arch Linux, or a portable AppImage (Arch, SteamOS / Steam Deck, or any
# distro with podman or docker). docs/linux-x86_64.md has the details.
#
#   scripts/build-linux.sh --rom "/path/to/Monster Hunter Tri (Europe) (En,Fr,De,Es,It).rvz"
#   scripts/build-linux.sh --rom "/path/to/...rvz" --appimage [--install]
#
# Native build: check the CPU, install packages with pacman, fetch nodtool,
# extract the disc, build the translator, translate the game, build MH3 and
# write the ./mh3 launcher.
#
# --appimage: the translator, the game build and the packaging run in an
# Ubuntu 22.04 container (packaging/linux/Containerfile, glibc 2.35), so the
# AppImage runs on SteamOS and on distros older than Arch. The host needs only
# podman or docker, git, curl and python3; on SteamOS (where pacman is
# read-only) --appimage is chosen automatically and podman is preinstalled.
#
# Every step is skipped when its result is already there, so re-running after
# a failure picks up where it stopped. Needs your own European disc (RMHP08)
# as ISO or RVZ; the AppImage holds no disc data and finds the image on its
# first start (next to itself, in ~/Emulation/roms/wii, or --rom).
#
# Options:
#   --rom PATH       disc image (needed until generated/ exists, to extract the game code)
#   --appimage       build dist/MH3-<version>-x86_64.AppImage in a container
#   --install        with --appimage: copy it to ~/Applications and add a menu entry
#   --engine NAME    container engine for --appimage: podman or docker (default: podman if present)
#   --dev            development build (stats, debug tools; no ThinLTO/PGO)
#   --no-pgo         release build without the checked-in PGO profile
#   --native         -march=native instead of x86-64-v3 (binary only runs on this PC; not with --appimage)
#   --no-deps        skip pacman
#   --retranslate    run the translator again even if generated/ exists
#   --clean-assets   delete the extracted disc (Assets/, ~5 GB) after a successful build
#   --jobs N         parallel jobs (default: all CPUs)
set -euo pipefail

NOD_TAG="v2.0.0-alpha.12"
NOD_SHA256="ee8345c004248eb82a86dd0c6068e0fcf3dc75baf68d18fe949bf4589822f5a5"
LLVM_VERSION="${MH3_LLVM_VERSION:-21}"

dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rom=""
dev=0 no_pgo=0 native=0 no_deps=0 retranslate=0 appimage=0 install=0 clean_assets=0
engine=""
jobs="$(nproc)"

die() { printf '\n\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }
step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
note() { printf '    %s\n' "$*"; }

while [[ $# -gt 0 ]]; do
  case $1 in
    --rom) rom="${2:?--rom needs a path}"; shift 2 ;;
    --appimage) appimage=1; shift ;;
    --install) install=1; shift ;;
    --engine) engine="${2:?--engine needs podman or docker}"; shift 2 ;;
    --dev) dev=1; shift ;;
    --no-pgo) no_pgo=1; shift ;;
    --native) native=1; shift ;;
    --no-deps) no_deps=1; shift ;;
    --retranslate) retranslate=1; shift ;;
    --clean-assets) clean_assets=1; shift ;;
    --jobs) jobs="${2:?--jobs needs a number}"; shift 2 ;;
    -h|--help) sed -n '2,35p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option: $1 (see --help)" ;;
  esac
done

# Absolute path before the script changes directory.
[[ -z $rom ]] || rom="$(realpath -m "$rom")"

[[ $EUID -ne 0 || ${MH3_ALLOW_ROOT:-0} == 1 ]] || die "run this as your normal user, not root (it calls sudo for pacman only)"
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die "this script is for x86-64 Linux"

os_id="$(. /etc/os-release 2>/dev/null; echo "${ID:-unknown}")"
if [[ $os_id == steamos ]]; then
  if [[ $appimage -eq 0 ]]; then
    note "SteamOS detected: building the AppImage in a container (pacman is read-only here)"
    appimage=1
  fi
  no_deps=1
fi
[[ $install -eq 0 || $appimage -eq 1 ]] || die "--install goes with --appimage"
[[ $native -eq 0 || $appimage -eq 0 ]] || die "--native makes a binary for this PC only; it does not go with --appimage"

# --- 1. CPU ------------------------------------------------------------------
step "Checking the CPU (the game needs x86-64-v3: AVX2 + FMA, Haswell / Zen 1 or newer)"
cpu_ok=0
if /usr/lib/ld-linux-x86-64.so.2 --help 2>/dev/null | grep -q 'x86-64-v3 (supported' ||
   /lib64/ld-linux-x86-64.so.2 --help 2>/dev/null | grep -q 'x86-64-v3 (supported'; then
  cpu_ok=1
else
  flags="$(grep -m1 '^flags' /proc/cpuinfo || true)"
  cpu_ok=1
  for f in avx avx2 fma bmi1 bmi2 f16c movbe ssse3; do
    [[ " $flags " == *" $f "* ]] || { cpu_ok=0; note "missing CPU feature: $f"; }
  done
fi
if [[ $cpu_ok -eq 1 ]]; then
  note "ok"
elif [[ $appimage -eq 1 ]]; then
  note "warning: this CPU can build the AppImage but cannot run it"
else
  die "this CPU is older than x86-64-v3; the x86-64 build needs FMA3/AVX2"
fi

# --- 2. Packages -------------------------------------------------------------
if [[ $no_deps -eq 0 ]]; then
  command -v pacman >/dev/null || die "pacman not found: this is for Arch; use --appimage (podman/docker) or --no-deps"
  step "Installing packages with pacman"
  if [[ $appimage -eq 1 ]]; then
    pkgs=(git curl python)
    if [[ -z $engine ]] && ! command -v podman >/dev/null && ! command -v docker >/dev/null; then
      pkgs+=(podman)
    fi
  else
    pkgs=(
      base-devel git curl python cmake ninja clang lld llvm
      dotnet-sdk-8.0
      openssl zlib libpng freetype2 sqlite zstd
      mesa libglvnd
      libx11 libxext libxrandr libxcursor libxi libxss libxtst libxfixes libxkbcommon
      wayland wayland-protocols libdecor
      alsa-lib libpulse systemd-libs dbus
    )
  fi
  sudo pacman -S --needed --noconfirm "${pkgs[@]}"
fi

if [[ $appimage -eq 1 ]]; then
  host_tools=(git curl python3 sha256sum)
else
  host_tools=(git curl python3 cmake ninja clang clang++ ld.lld llvm-ar dotnet sha256sum)
fi
for tool in "${host_tools[@]}"; do
  command -v "$tool" >/dev/null || die "'$tool' not found (install it, or drop --no-deps)"
done

if [[ $appimage -eq 1 ]]; then
  if [[ -z $engine ]]; then
    if command -v podman >/dev/null; then engine=podman; elif command -v docker >/dev/null; then engine=docker; fi
  fi
  [[ $engine == podman || $engine == docker ]] || die "--appimage needs podman or docker (SteamOS 3.5+ ships podman; on Arch: sudo pacman -S podman)"
  command -v "$engine" >/dev/null || die "$engine not found"
  note "container engine: $engine"
else
  clang_major="$(clang -dumpversion | cut -d. -f1)"
  note "clang $clang_major, cmake $(cmake --version | head -1 | awk '{print $3}'), dotnet $(dotnet --version)"
  dotnet --list-sdks | grep -q '^8\.' || note "warning: no .NET 8 SDK listed; a newer SDK can usually still build net8.0"
  if [[ $dev -eq 0 && $no_pgo -eq 0 && $clang_major -lt 21 ]]; then
    note "clang < 21: the release build skips the PGO profile automatically"
  fi
fi

# --- 3. Source ---------------------------------------------------------------
cd "$dir"
[[ -f scripts/translate.sh && -d runtime ]] || die "run this from an mh3-wii checkout (scripts/build-linux.sh)"

# --- 4. nodtool --------------------------------------------------------------
step "Fetching nodtool $NOD_TAG"
mkdir -p tools
if [[ -x tools/nodtool ]] && echo "$NOD_SHA256  tools/nodtool" | sha256sum -c --status; then
  note "already there"
else
  curl -fL --retry 3 -o tools/nodtool.part \
    "https://github.com/encounter/nod/releases/download/$NOD_TAG/nodtool-linux-x86_64"
  echo "$NOD_SHA256  tools/nodtool.part" | sha256sum -c --status || die "nodtool checksum mismatch"
  mv tools/nodtool.part tools/nodtool
  chmod +x tools/nodtool
fi

# --- 5. Disc -----------------------------------------------------------------
need_disc=1
if [[ $retranslate -eq 0 && -f generated/translation.json && -f generated/build_shards/shards.cmake ]]; then
  need_disc=0   # the game code is already translated; the build no longer reads the disc
fi
step "Extracting the disc"
if [[ -f Assets/DATA/sys/main.dol ]]; then
  note "Assets/DATA already extracted"
elif [[ $need_disc -eq 0 ]]; then
  note "not needed: generated/ is already there"
else
  [[ -n $rom ]] || die "pass --rom with your European Monster Hunter Tri image (ISO or RVZ)"
  [[ -f $rom ]] || die "disc image not found: $rom"
  rm -rf Assets/DATA
  mkdir -p Assets
  tools/nodtool extract -q "$rom" Assets/DATA
  [[ -f Assets/DATA/sys/main.dol ]] || die "extraction finished but Assets/DATA/sys/main.dol is missing"
  game_id="$(head -c 6 Assets/DATA/sys/boot.bin 2>/dev/null || true)"
  [[ $game_id == RMHP08 ]] || die "this image is '$game_id', not the European disc RMHP08 (the only one mh3-wii supports)"
fi

# The offline Vita texture packer (a host tool for the Vita build) needs an extra
# stb header and is not part of MH3; the README turns it off for Linux too.
cmake_args=(-DMH3_BUILD_VITA_TEXTURE_PACKER=OFF)
[[ $dev -eq 0 ]] || cmake_args+=(-DMH3_DEVELOPMENT=ON)
[[ $no_pgo -eq 0 ]] || cmake_args+=(-DMH3_PGO_RELEASE_PROFILE=OFF)
[[ $native -eq 0 ]] || cmake_args+=(-DMH3_X86_64_MARCH=native)

clean_assets_now() {
  if [[ $clean_assets -eq 1 && -d Assets ]]; then
    step "Removing the extracted disc (Assets/)"
    rm -rf Assets
    note "removed; pass --rom again if you ever need --retranslate"
  fi
}

# =============================================================================
# AppImage: translator, build and packaging in an Ubuntu 22.04 container
# =============================================================================
if [[ $appimage -eq 1 ]]; then
  mkdir -p dist
  containerfile=packaging/linux/Containerfile
  image_tag="mh3-wii-build:$(cat "$containerfile" <(echo "LLVM_VERSION=$LLVM_VERSION") | sha256sum | cut -c1-12)"
  step "Preparing the build container ($image_tag; the first time downloads ~2 GB)"
  if "$engine" image inspect "$image_tag" >/dev/null 2>&1; then
    note "already built"
  else
    "$engine" build --build-arg "LLVM_VERSION=$LLVM_VERSION" -t "$image_tag" -f "$containerfile" packaging/linux ||
      die "building the container image failed (podman on Arch needs subuid/subgid entries: sudo usermod --add-subuids 100000-165535 --add-subgids 100000-165535 \$USER)"
  fi

  build_dir="build/appimage-x86_64"
  [[ $dev -eq 0 ]] || build_dir="build/appimage-x86_64-dev"
  version="$(git describe --always --dirty 2>/dev/null || echo unknown)"
  out="dist/MH3-${version}-x86_64.AppImage"

  step "Translating and building in the container (several minutes to an hour)"
  run_args=(run --rm -v "$dir:$dir" -w "$dir" -e TERM=dumb)
  if [[ $engine == podman ]]; then
    run_args+=(--userns=keep-id --security-opt label=disable)
  else
    run_args+=(--user "$(id -u):$(id -g)")
  fi
  run_args+=(-e MH3_BUILD_DIR="$build_dir" -e MH3_APPIMAGE_OUT="$out" -e MH3_APPIMAGE_VERSION="$version"
             -e MH3_CMAKE_ARGS="${cmake_args[*]}" -e MH3_RETRANSLATE="$retranslate" -e MH3_JOBS="$jobs")
  "$engine" "${run_args[@]}" "$image_tag" bash "$dir/scripts/linux/appimage-build.sh" ||
    die "container build failed; logs: $dir/logs/ (aurora-configure.log, aurora-build.log, translate.log)"
  [[ -f $out ]] || die "the build finished but $out is missing"
  chmod +x "$out"
  ln -sfn "$(basename "$out")" dist/MH3-x86_64.AppImage
  clean_assets_now

  if [[ $install -eq 1 ]]; then
    step "Installing the AppImage"
    mkdir -p "$HOME/Applications" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/128x128/apps"
    cp -f "$out" "$HOME/Applications/MH3.AppImage"
    chmod +x "$HOME/Applications/MH3.AppImage"
    cp -f runtime/sce_sys/icon0.png "$HOME/.local/share/icons/hicolor/128x128/apps/mh3-wii.png"
    sed "s|^Exec=.*|Exec=\"$HOME/Applications/MH3.AppImage\"|" packaging/linux/mh3-wii.desktop \
      > "$HOME/.local/share/applications/mh3-wii.desktop"
    note "installed: ~/Applications/MH3.AppImage (menu entry: Monster Hunter Tri)"
  fi

  step "Done"
  note "AppImage: $dir/$out (also dist/MH3-x86_64.AppImage)"
  note "Run:      $out   (first start: it finds the disc next to itself or in ~/Emulation/roms/wii, or pass --rom /path/to/image.rvz)"
  note "Data:     ~/.local/share/mh3-wii/ (saves/, disc/, mh3.log)"
  note "Steam Deck: in Desktop Mode, right-click the AppImage (or the menu entry) > Add to Steam; in Game Mode it uses the Deck's controls through Steam Input"
  exit 0
fi

# =============================================================================
# Native build on the host (Arch)
# =============================================================================

# --- 6. Translator -----------------------------------------------------------
step "Building the translator (.NET)"
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
dotnet build -c Release toolchain/translator/src/Translator.Cli 2>&1 | tail -n 3
DOTNET_BIN="$(command -v dotnet)"
export DOTNET_BIN

# --- 7. Translate ------------------------------------------------------------
step "Translating the game (PowerPC -> C++, several minutes)"
if [[ $need_disc -eq 0 ]]; then
  note "generated/ already present (use --retranslate to redo)"
else
  TRANSLATE_JOBS="$jobs" scripts/translate.sh || die "translation failed; see $dir/logs/"
fi

# --- 8. Build ----------------------------------------------------------------
build_dir="build/linux-x86_64"
[[ $dev -eq 0 ]] || build_dir="build/linux-x86_64-dev"

step "Building MH3 ($build_dir; this is the long part)"
if ! MH3_ARCH=x86_64 MH3_BUILD_DIR="$build_dir" BUILD_JOBS="$jobs" \
     MH3_EXTRA_CMAKE_ARGS="${cmake_args[*]:-}" scripts/build-aurora.sh; then
  echo
  tail -n 25 logs/aurora-configure.log 2>/dev/null | sed 's/^/    configure| /' || true
  tail -n 40 logs/aurora-build.log 2>/dev/null | sed 's/^/    build| /' || true
  die "build failed; full logs: $dir/logs/aurora-configure.log and $dir/logs/aurora-build.log"
fi
[[ -x $build_dir/MH3 ]] || die "build finished but $build_dir/MH3 is missing"
clean_assets_now

# --- 9. Launcher -------------------------------------------------------------
step "Writing the launcher"
launcher="$dir/mh3"
{
  echo '#!/usr/bin/env bash'
  echo '# Monster Hunter Tri (mh3-wii, x86-64). Extra arguments go to MH3, e.g. --village or --list quests.'
  echo "cd \"$dir\""
  echo "rom=\"$rom\""
  echo "recorded=\"\$(head -n1 \"$dir/$build_dir/disc/rom.txt\" 2>/dev/null || true)\""
  echo 'args=()'
  echo '# Prepare the disc data on the first start, or when the image moved.'
  echo 'if [[ -n $rom && $recorded != "$rom" && " $* " != *" --rom "* ]]; then args=(--rom "$rom"); fi'
  echo "exec \"$dir/$build_dir/MH3\" \"\${args[@]}\" \"\$@\""
} > "$launcher"
chmod +x "$launcher"

step "Done"
note "Run:   $launcher            (or $launcher --village to go straight to the village)"
note "Saves: $dir/saves/"
note "Keys:  Space=A  LShift=B  WASD=left stick  IJKL=right stick  arrows=D-pad  Enter=Plus; SDL gamepads work"
[[ -n $rom ]] || note "No --rom given this run: if MH3 asks for the disc, run: $launcher --rom /path/to/image.rvz"
