// PC settings file: defaults, round trip, validation (pc_settings.cpp).
#include "pc_settings.h"

#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
} // namespace

int main() {
    using namespace PcSettings;
    const Settings defaults = Defaults();
    check(defaults.keyboard[size_t(Control::A)][0] == "key:Space", "default keyboard A is Space");
    check(defaults.gamepad[size_t(Control::A)][0] == "pad:b", "default pad A is east (b)");
    check(defaults.gamepad[size_t(Control::L)][0] == "axis:lefttrigger+", "default L is the left trigger");

    // Round trip of a changed value set.
    Settings changed = defaults;
    changed.displayMode = DisplayMode::Borderless;
    changed.resolution = 3;
    changed.msaa = 4;
    changed.anisotropy = 16;
    changed.widescreen = false;
    changed.frameRate = FrameRate::Capped;
    changed.fpsCap = 144;
    changed.showFps = true;
    changed.keyboard[size_t(Control::B)] = {"key:Left Shift", "mouse:right"};
    changed.gamepad[size_t(Control::ZL)] = {"", ""};
    changed.gamepadDevice = "030000005e0400008e02000014010000";
    changed.mouseLook = true;
    changed.mouseSensitivity = 2.5f;
    changed.enabledMods = {"HD UI", "Sharper Felynes, v2"};
    changed.cheats["No damage"] = "";
    changed.cheats["Monster HP multiplier"] = "x2";
    changed.geckoCodes = {"Infinite Health", "Max Zenny, Points"};
    changed.hdTextures = false;
    changed.dumpTextures = true;
    const auto text = Serialize(changed);
    std::vector<std::string> warnings;
    const Settings parsed = Parse(text, defaults, &warnings);
    check(warnings.empty(), "serialized settings parse without warnings");
    check(parsed.displayMode == DisplayMode::Borderless && parsed.resolution == 3 && parsed.msaa == 4 && parsed.anisotropy == 16, "graphics round trip");
    check(!parsed.widescreen && parsed.frameRate == FrameRate::Capped && parsed.fpsCap == 144 && parsed.showFps, "frame rate round trip");
    check(parsed.keyboard[size_t(Control::B)][0] == "key:Left Shift" && parsed.keyboard[size_t(Control::B)][1] == "mouse:right", "two keyboard sources");
    check(parsed.gamepad[size_t(Control::ZL)][0].empty() && parsed.gamepad[size_t(Control::ZL)][1].empty(), "unbound control");
    check(parsed.gamepadDevice == changed.gamepadDevice && parsed.mouseLook && parsed.mouseSensitivity == 2.5f, "input round trip");
    check(parsed.enabledMods == changed.enabledMods, "mod list round trip (spaces kept)");
    check(parsed.cheats == changed.cheats && parsed.geckoCodes == changed.geckoCodes, "cheats round trip");
    check(!parsed.hdTextures && parsed.dumpTextures, "texture pack settings round trip");
    // A file's mod lines replace the base's list rather than adding to it.
    check(Parse("mod = Only This\n", parsed).enabledMods == std::vector<std::string>{"Only This"}, "mod lines replace the base list");
    check(Serialize(parsed) == text, "serialize is stable");

    // Invalid values are skipped with a warning; the base value stays.
    warnings.clear();
    const Settings bad = Parse("graphics.msaa = 3\ngraphics.resolution = 99\nkeyboard.a = joystick:7\nbogus = 1\ngraphics.fps_cap = 10\nno equals sign\n", defaults, &warnings);
    check(warnings.size() == 6, "six warnings for six bad lines");
    check(bad.msaa == 1 && bad.resolution == 0 && bad.keyboard[0][0] == "key:Space" && bad.fpsCap == 60, "bad values keep defaults");

    if (failures)
        return 1;

    std::puts("pc_settings: all checks passed");
    return 0;
}
