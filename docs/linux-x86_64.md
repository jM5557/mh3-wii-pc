# x86-64 Linux: Arch, SteamOS / Steam Deck, AppImage

`scripts/build-linux.sh` builds the game from your own European disc
(`RMHP08`) in one of two ways:

- **Native** (default on Arch): installs the packages with pacman, builds with
  the host toolchain and writes the `./mh3` launcher. The binary runs on this
  machine's distro.
- **AppImage** (`--appimage`; automatic on SteamOS): runs the translator, the
  build and the packaging in an Ubuntu 22.04 container
  ([packaging/linux/Containerfile](../packaging/linux/Containerfile)). The
  result, `dist/MH3-<version>-x86_64.AppImage`, runs on SteamOS and on any
  distro from 2022 on (glibc 2.35+).

Each step is skipped when its result already exists, so after a failure you
can run the same command again and it continues from where it stopped.

## Requirements

- An x86-64-v3 CPU: Intel Haswell (2013) / AMD Zen 1 (2017) or newer, which
  includes the Steam Deck. The paired-single code lowers `ps_madd` and friends
  to FMA3 and psq byte swaps to SSSE3, so the build uses `-march=x86-64-v3`
  (`MH3_X86_64_MARCH`, or `--native` for this CPU only).
- A GPU driver with OpenGL ES 3.0 through EGL: Mesa (AMD, Intel, the Deck) or
  NVIDIA's driver.
- About 15 GB free while building: the extracted disc (`Assets/`, about 5 GB,
  removable afterwards with `--clean-assets`), `generated/`, the build tree, and
  the AppImage build container (about 3 GB).

## Arch Linux, native build

```sh
scripts/build-linux.sh --rom 'Monster Hunter Tri (Europe) (En,Fr,De,Es,It).rvz'
./mh3                 # or ./mh3 --village, ./mh3 --list quests, ...
```

The packages it installs with `pacman -S --needed`:

```
base-devel git curl python cmake ninja clang lld llvm dotnet-sdk-8.0
openssl zlib libpng freetype2 sqlite zstd mesa libglvnd
libx11 libxext libxrandr libxcursor libxi libxss libxtst libxfixes libxkbcommon
wayland wayland-protocols libdecor alsa-lib libpulse systemd-libs dbus
```

It downloads nodtool (checksum-pinned) into `tools/`, extracts the disc into
`Assets/DATA`, builds the .NET translator, runs `scripts/translate.sh` and
then `scripts/build-aurora.sh` with `MH3_ARCH=x86_64` into
`build/linux-x86_64/`. Saves go to `saves/` in the checkout. With clang 21 or
newer the release build uses the checked-in PGO profile; older clang builds
without it.

## AppImage (Arch, SteamOS / Steam Deck, any distro with podman or docker)

```sh
scripts/build-linux.sh --rom 'Monster Hunter Tri (Europe) (En,Fr,De,Es,It).rvz' --appimage --install
```

The host needs podman or docker, git, curl and python3. On Arch the script
installs podman if neither is there; rootless podman needs subuid/subgid
entries, which the script prints if the container build fails because of them.
SteamOS 3.5 and newer ship podman. Building on a desktop PC and copying the
AppImage to the Deck is much faster than building on the Deck.

`--install` copies the AppImage to `~/Applications/MH3.AppImage` and adds a
"Monster Hunter Tri" menu entry. Without it, run `dist/MH3-x86_64.AppImage`
(a link to the newest build).

How the AppImage is put together:

- The container ([packaging/linux/Containerfile](../packaging/linux/Containerfile))
  has clang 21 from apt.llvm.org, Ubuntu's libstdc++ 12, the .NET 8 SDK,
  Python 3.11 (the scripts need `hashlib.file_digest`) and CMake from PyPI.
  The image is tagged with a hash of the Containerfile, so it is rebuilt only
  when that file changes.
- [scripts/linux/appimage-build.sh](../scripts/linux/appimage-build.sh) runs
  inside it. It links libstdc++ and libgcc statically with
  `-Wl,--exclude-libs,ALL`, so the host's Mesa keeps its own newer libstdc++.
  linuxdeploy bundles OpenSSL only. Graphics, audio, X11/Wayland, input and
  glibc come from the host, so Mesa and the Deck's drivers stay in charge.
- [packaging/linux/AppRun](../packaging/linux/AppRun) is the launcher. It does
  the following:
  - sets `MH3_USER_DIR` and `MH3_DISC_HOME` under
    `${XDG_DATA_HOME:-~/.local/share}/mh3-wii/`, because the AppImage itself is
    read-only;
  - points the bundled OpenSSL at the host's CA certificates;
  - sets `MH3_FULLSCREEN=1` under gamescope;
  - finds the disc on the first start;
  - appends the game's output to `mh3.log`.

### Playing the AppImage

On its first start, or after the image has moved, the launcher looks for the
disc in these places:

- next to the AppImage;
- `~/.local/share/mh3-wii/`;
- `~/Emulation/roms/wii` (EmuDeck, on the internal drive or an SD card);
- `~/Games`;
- `~/Downloads`.

An `.iso` is recognized by its disc ID, so any name works. An `.rvz` or `.wia`
needs `RMHP08` or "Monster Hunter Tri" in its name. You can also pass
`--rom /path/to/image` once; the game remembers it. Other arguments go to MH3
as on any Linux build (`--village`, `--quest`, `--list quests`, `--cheat`, ...).

Data lives in `~/.local/share/mh3-wii/`:

- `saves/`: the saves;
- `disc/`: the disc metadata and the converted-asset cache. MH3 creates it on
  the first `--rom`;
- `mh3.log`: the game's output, which is the place to look when a start from
  Game Mode fails.

To keep everything next to the AppImage instead, for example on a USB stick,
run it once with `--appimage-portable-home`.

Steam Deck: in Desktop Mode, add the AppImage, or the menu entry from
`--install`, to Steam (right-click, Add to Steam). In Game Mode it starts
fullscreen and uses the Deck's controls through Steam Input. Elsewhere,
Alt+Enter toggles fullscreen.

## Options

| Option | Effect |
| --- | --- |
| `--rom PATH` | disc image; needed until `generated/` exists |
| `--appimage` | build the AppImage in a container |
| `--install` | with `--appimage`: install to `~/Applications` with a menu entry |
| `--engine podman\|docker` | container engine (default: podman if present) |
| `--dev` | development build (`MH3_DEVELOPMENT`: stats, debug tools; no ThinLTO/PGO) |
| `--no-pgo` | release build without the PGO profile |
| `--native` | `-march=native` (native build only) |
| `--no-deps` | skip pacman |
| `--retranslate` | run the translator again |
| `--clean-assets` | delete `Assets/` after a successful build |
| `--jobs N` | parallelism (default: all CPUs) |

## Source changes for x86-64 Linux

- `runtime/CMakeLists.txt`: x86-64 Linux is a supported host, built with
  `-march=${MH3_X86_64_MARCH}` (default `x86-64-v3`) for every target.
- `runtime/include/isa/ppc_isa_float.h`: a clear `#error` when FMA3/SSSE3 are
  missing.
- `scripts/target.sh`: `MH3_ARCH=x86_64` (native builds only), the default on
  an x86-64 Linux machine.
- `runtime/src/efb_copy_trace.cpp`: x86-64 fault decoding.
- `runtime/src/main.cpp`: `MH3_DISC_HOME` (see [flags](flags.md)).
- `runtime/src/graphics_host.cpp`: `MH3_FULLSCREEN` (see [flags](flags.md)).
- `aurora-main/include/aurora/fast_map.h`: includes `<tuple>`, which
  libstdc++ 12 does not pull in through `<utility>`.
