# PC features

The Linux / Steam Deck build has an in-game settings menu, rebindable
keyboard, mouse and gamepad controls (Wii Remotes included), file-replacement
mods, Dolphin-format HD texture packs, the native cheat list and Gecko codes.

## Where things go

Everything lives next to the save directory (`MH3_USER_DIR`):

| What | AppImage | Built from source (run in the repo) |
| --- | --- | --- |
| Settings | `~/.local/share/mh3-wii/saves/settings.ini` | `saves/settings.ini` |
| Mods | `~/.local/share/mh3-wii/mods/` | `mods/` |
| HD textures | `~/.local/share/mh3-wii/Load/Textures/RMHP08/` | `Load/Textures/RMHP08/` |
| Texture dumps | `~/.local/share/mh3-wii/Dump/Textures/RMHP08/` | `Dump/Textures/RMHP08/` |
| Gecko codes | `~/.local/share/mh3-wii/codes/` | `codes/` |

The data folder is the parent of `MH3_USER_DIR`, or the working directory
when that variable isn't set. The menu's About tab lists the real paths, and
each one has an Open button.

## The menu

Press **F1** to open or close it, or **L3 + R3** (both stick clicks) on any
gamepad. That includes the Steam Deck, whose built-in controls appear as an
Xbox-style pad. The game gets no input while the menu is open, and the game
keeps running.

| | Mouse | Keyboard | Gamepad |
| --- | --- | --- | --- |
| Move | point | arrows, Tab | d-pad, left stick |
| Choose | click | Enter / Space | A (south) |
| Back / close | the X | Escape | B (east), L3+R3 |
| Change tab | click the tab | Q / E | LB / RB |
| Clear a binding | right-click | Delete | X (west) |

Changes save to `settings.ini` as you make them, and again when the menu
closes. Settings marked *(restart)* apply at the next start; a banner says
when one is waiting.

### Graphics

- **Display mode**: windowed, borderless fullscreen or exclusive fullscreen.
  Applies at once. Alt+Enter still toggles fullscreen.
- **Render resolution**: *Default* keeps the port's 960x544 surface scaled to
  the window. *Match window* renders one pixel per window pixel. *N× Wii*
  renders at N times 640x448. Applies at once.
- **Anti-aliasing**: off, 2× or 4× MSAA *(restart)*.
- **Texture filtering**: trilinear, or 2×–16× anisotropic *(restart)*.
- **Widescreen**: the game's own 16:9 mode *(restart)*.
- **Frame rate**: see below.
- **Show FPS**: a small counter in the corner.
- **Menu size**: *Auto* scales with the window height: 1× at 720 lines,
  about 1.1× on the Deck. A system font is used when one is installed
  (DejaVu Sans, Noto Sans, Liberation Sans; `MH3_UI_FONT=/path/font.ttf`
  picks one).

### Frame rate and game speed

The game's logic was written for 30 steps a second. Its timers count frames,
not seconds.

- **Unlocked (display rate)** is the default. The game advances each frame
  by that frame's real time, through the variable-step sites in
  [variable-step-sites.md](variable-step-sites.md). That covers the common
  timers, motions, the camera and effects. A timer the port hasn't found yet
  still counts frames, so it runs faster at higher frame rates.
- **Unlocked, capped** is the same, held to at most the FPS cap (30–360).
  If an action looks too fast, cap at 60 to keep the difference small while
  it's reported.
- **Unlimited** is the same with no limit at all, not even the display's.
- **30 fps (original timing)** runs the game's own fixed 30 Hz steps. Every
  action runs exactly at the Wii's speed.

To report an action that speeds up, run the same action at 30 fps (original
timing) and at an unlocked rate, and note what changes: duration, distance
or animation speed. With a development build, `MH3_TRACE=input` logs the
player's action and motion numbers each tick, which pins down the timer
([flags.md](flags.md)).

### Controls

- **Device**: *First connected*, or a specific gamepad (remembered by its SDL
  GUID). Any controller SDL knows works: XInput and DirectInput pads, PlayStation
  and Switch pads, Steam Input virtual pads, and the Steam Deck's own
  controls.
- **Stick dead zone**, **Swap sticks**.
- **Wii Remotes (Bluetooth)** *(restart)*: turns on SDL's Wii Remote driver.
  - Pair the remote with your PC's Bluetooth first: press the red sync
    button, then pair as "Nintendo RVL-CNT-01".
  - A remote with a Classic Controller, or with a Nunchuk, shows up as a
    gamepad you can choose and bind like any other.
  - The pointer (IR) and motion aren't used; the game itself only reads a
    Classic Controller.
  - On Linux the user needs read/write access to the remote's `hidraw`
    device. Steam's udev rules, or the `game-devices-udev` package, grant
    it.
- **Mouse look** turns the camera with the mouse, as the right stick, with a
  sensitivity and an invert option. The cursor is captured while the menu is
  closed.
- **Bindings**: two keyboard (or mouse button) sources and two gamepad
  sources per Classic Controller input.
  - Select a cell, then press the key or button. Escape cancels; a gamepad
    capture also ends on its own after 5 seconds.
  - Binding a source moves it off any other control that had it.
  - *Reset keyboard* and *Reset gamepad* restore the defaults.

Default keyboard: Space = A, Left Shift = B, E = X, F = Y, Q = L, R = R,
Z = ZL, C = ZR, Enter = +, Backspace = −, Escape = Home, arrows = d-pad,
WASD = left stick, IJKL = right stick.

## Mods

A mod is a folder in `mods/` holding replacement game files at their disc
paths. A folder named like a Dolphin extraction also works:

```
mods/
  Quest Pack/
    mod.ini                 (optional)
    05/quest00_eng.bin      (replaces 05/quest00_eng.bin)
  HD Village UI/
    files/05/ui.arc         (a Dolphin "files/" layout works too)
```

`mod.ini` is optional; it can hold `title`, `author`, `version` and
`description` lines (`key = value`). Turn mods on in **Mods & textures**,
and order them with the arrows: a later mod wins when two replace the same
file. Mods apply at the next start.

- Replacement files can be **any size**. The file table is rewritten and the
  new file is served from the mod folder; nothing is copied into the disc
  image.
- Names match disc paths case-insensitively. A file that matches nothing is
  skipped and logged (`MH3 mods: … matches no disc file`).
- Each set of enabled mods gets its own converted-asset cache
  (`disc/cache-<id>/`), so turning mods on or off never mixes old and new
  conversions. The first start with a new set converts the replaced
  textures again.
- `main.dol` and the `sys/` files can't be modded: the game's code is
  recompiled from them at build time. Code mods go in the source.

## HD texture packs (Dolphin format)

Put a Dolphin pack for **Monster Hunter Tri (PAL), game ID RMHP08** in
`Load/Textures/RMHP08/` at any folder depth. These are the same file names
Dolphin uses:

```
tex1_{width}x{height}[_m]_{texture hash}[_{palette hash}]_{format}.dds|.png
```

- Hashes are XXH64 of the texture's base level, and of the palette entries it
  uses, as Dolphin computes them. `$` wildcards for the texture or palette
  hash work.
- **DDS**: uncompressed RGBA/BGRA, or BC1 (DXT1), BC2 (DXT3), BC3 (DXT5),
  BC4, BC5 or BC7. Mip chains stored in the file are used. Block-compressed
  images are decoded to RGBA8 on load, since GLES 3.0 doesn't guarantee BC
  support.
- **PNG**, with optional `_mip1`, `_mip2` … files. A single-level replacement
  for a mipmapped texture gets box-filtered mips.
- Packs made for the **NTSC-U (RMHE08)** or **NTSC-J (RMHJ08)** releases
  match wherever the texture data is identical. Text and region-specific
  textures won't match.

Packs are read when a texture is first shown. A large DDS can cause a short
hitch the first time, as in Dolphin without prefetching. Up to 2 GiB of
textures stay resident. Turn **Load HD texture packs** off to compare
*(restart)*.

**Making a pack:** turn on **Dump textures** *(restart)*. Each texture the
game shows is written once to `Dump/Textures/RMHP08/` under its pack name.
Upscale or edit those files, keep the names, and put them in
`Load/Textures/RMHP08/`.

## Cheats and Gecko codes

The **Cheats** tab lists the port's native cheats by group (see
[cheats.md](cheats.md)). They apply at once and are remembered in
`settings.ini` (`cheat.Group:Name = value`). `--cheat` and `--no-damage`
add to the saved ones for one run.

Gecko codes come from Dolphin game-settings files (`RMHP08.ini` with a
`[Gecko]` section; `[Gecko_Enabled]` is honoured) or plain `.txt` lists in
`codes/`. Enabled codes run once a frame, like the Wii's code handler.

- These code types work: RAM writes (00–07), serial writes (08),
  conditions (20–2F with E0/E2 endifs), base and pointer codes (40–4F) and
  terminators.
- Codes that patch or insert PowerPC instructions can't change a static
  recompilation: C0–C2, D0–DE, or writes into the executable range
  0x80004000–0x8056FAA0. They're shown greyed out with the reason, and most
  have a native version in the cheat list.

## settings.ini

The menu writes it; hand edits are validated on load, and a bad line is
skipped with a warning on stderr. Keys:

```ini
graphics.display_mode = windowed        # windowed | borderless | fullscreen
graphics.resolution = auto              # auto | window | 1..8 (× 640x448)
graphics.msaa = 1                       # 1 | 2 | 4
graphics.anisotropy = 1                 # 1 | 2 | 4 | 8 | 16
graphics.widescreen = on
graphics.frame_rate = display           # display | exact30 | capped | unlimited
graphics.fps_cap = 60
graphics.show_fps = off

input.gamepad_device = any              # or an SDL GUID
input.dead_zone = 0.15
input.mouse_look = off
input.mouse_sensitivity = 1
input.invert_mouse_y = off
input.wii_remotes = off
input.swap_sticks = off

keyboard.a = key:Space | mouse:right    # up to two sources; "none" = unbound
gamepad.a = pad:b                       # pad:<button>, axis:<axis>+ or -
                                        # (one line per control: a, b, x, y, l, r, zl, zr,
                                        # plus, minus, home, up, down, left, right,
                                        # left_stick_up ... right_stick_right)

mod = Quest Pack                        # enabled mods, one a line, in load order
mod = HD Village UI
textures.hd = on
textures.dump = off
gecko = Infinite Health                 # enabled Gecko codes, one a line
cheat.Player:No damage = on             # cheat.<group>:<name> = on | <value>

ui.scale = 0                            # 0 = auto
```

## Tests

- `mh3_pc_settings_check`: the file format.
- `mh3_gecko_check`: the code engine.
- `mh3_disc_replacements_check`: mods on a small extracted disc: file table
  patch, reads and size changes.
- `mh3_sdl_controller_test`: bindings and hotplug.
- `mh3_texture_pack_names_check`: Dolphin pack names, with hashes computed
  outside the code under test. Covers palette ranges, `_m`, nested folders,
  generated mips and dump names.
