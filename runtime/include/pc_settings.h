#pragma once
// PC settings: what the in-game menu (pc_menu.h) changes, kept in
// <user dir>/settings.ini ("section.key = value" lines; MH3_USER_DIR, default
// saves/). Plain data and the file format only: the parts of the runtime that
// own each setting read it from here (graphics_host, frame_pacer,
// controller_host, mods). Every value is validated on load, so a hand-edited
// file can't put the game in a state the menu couldn't.
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace PcSettings {

enum class DisplayMode : uint8_t { Windowed, Borderless, Fullscreen };

// The game logic's pacing (frame_pacer.cpp).
//   Display: frames as fast as the display shows them (the port's default),
//            the game advanced by each frame's real time.
//   Exact30: the game's own fixed 30 Hz steps: exactly the Wii's speed for
//            every action, at 30 fps.
//   Capped:  like Display, at most fpsCap frames a second.
//   Unlimited: no limit at all.
enum class FrameRate : uint8_t { Display, Exact30, Capped, Unlimited };

// Classic Controller inputs the host maps controls to (controller_host.cpp).
enum class Control : uint8_t {
    A, B, X, Y, L, R, ZL, ZR, Plus, Minus, Home,
    Up, Down, Left, Right,
    LeftStickUp, LeftStickDown, LeftStickLeft, LeftStickRight,
    RightStickUp, RightStickDown, RightStickLeft, RightStickRight,
    Count,
};
constexpr size_t kControlCount = size_t(Control::Count);
const char* ControlName(Control control);  // settings key part: "a", "left_stick_up"
const char* ControlLabel(Control control); // menu text: "A", "Left stick up"

// A binding source, as text: "key:<SDL scancode name>", "mouse:left|right|middle|x1|x2|wheelup|wheeldown",
// "pad:<SDL gamepad button name>" or "axis:<SDL gamepad axis name>+|-"; in the file
// a control's sources are separated by " | " ("none": unbound). Two per
// control and device kind; empty strings are unbound.
using Sources = std::array<std::string, 2>;

struct Settings {
    // Graphics
    DisplayMode displayMode = DisplayMode::Windowed;
    // 0: sized for the Vita's 960x544 display surface (the port's default);
    // -1: the window's own pixels; 1..8: that many times the Wii's 640x448.
    int resolution = 0;
    int msaa = 1;       // 1, 2 or 4 (at startup)
    int anisotropy = 1; // 1, 2, 4, 8 or 16 (at startup)
    bool widescreen = true; // the game's 16:9 mode (at startup)
    FrameRate frameRate = FrameRate::Display;
    int fpsCap = 60;    // Capped: 30..360
    bool showFps = false;

    // Controls
    std::array<Sources, kControlCount> keyboard{};
    std::array<Sources, kControlCount> gamepad{};
    std::string gamepadDevice; // "" any (the first connected), else an SDL GUID string
    float deadZone = 0.15f;     // radial, 0..0.5
    bool mouseLook = false;     // mouse movement turns the camera (right stick)
    float mouseSensitivity = 1.0f; // 0.1..5
    bool invertMouseY = false;
    bool wiiRemotes = false;    // SDL's HIDAPI Wii Remote driver (at startup)
    bool swapSticks = false;

    // Mods, textures, cheats
    std::vector<std::string> enabledMods; // folder names under mods/, in load order
    bool hdTextures = true;               // Load/Textures/RMHP08 (Dolphin packs)
    bool dumpTextures = false;            // Dump/Textures/RMHP08, for making packs
    std::map<std::string, std::string> cheats;  // cheat name -> value label ("" plain toggle)
    std::vector<std::string> geckoCodes;        // enabled Gecko code names

    // Menu
    float uiScale = 0.f; // 0: from the window height
};

Settings Defaults();

// The live settings. Load once at startup (before the window opens); the
// menu edits them in place and calls Save.
Settings& Get();
std::filesystem::path File();
void Load();
bool Save(); // false (and a message on stderr) when the file can't be written

// The file format, exposed for tests: parse text into a settings value
// starting from `base`; unknown keys and invalid values are skipped and
// reported in `warnings`.
Settings Parse(std::string_view text, const Settings& base, std::vector<std::string>* warnings = nullptr);
std::string Serialize(const Settings& settings);

// Settings that only take effect at the next start differ from what the
// running game started with.
bool RestartPending();
void MarkStarted(); // the startup values are the current ones

} // namespace PcSettings
