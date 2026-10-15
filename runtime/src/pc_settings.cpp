// PC settings and their file (pc_settings.h).
#include "pc_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace PcSettings {
namespace {

struct ControlInfo {
    const char* name;
    const char* label;
    const char* key0;
    const char* key1;
    const char* pad0;
    const char* pad1;
};

// The defaults are the port's original fixed controls (controller_host.cpp).
// Face buttons by position: east = A, south = B, north = X, west = Y, as on
// the Classic Controller; the analog triggers are L/R, the bumpers ZL/ZR.
constexpr ControlInfo kControls[kControlCount] = {
    {"a", "A", "key:Space", "", "pad:b", ""},
    {"b", "B", "key:Left Shift", "", "pad:a", ""},
    {"x", "X", "key:E", "", "pad:y", ""},
    {"y", "Y", "key:F", "", "pad:x", ""},
    {"l", "L", "key:Q", "", "axis:lefttrigger+", ""},
    {"r", "R", "key:R", "", "axis:righttrigger+", ""},
    {"zl", "ZL", "key:Z", "", "pad:leftshoulder", ""},
    {"zr", "ZR", "key:C", "", "pad:rightshoulder", ""},
    {"plus", "+ (Start)", "key:Return", "", "pad:start", ""},
    {"minus", "- (Select)", "key:Backspace", "", "pad:back", ""},
    {"home", "Home", "key:Escape", "key:Home", "pad:guide", ""},
    {"up", "D-pad up", "key:Up", "", "pad:dpup", ""},
    {"down", "D-pad down", "key:Down", "", "pad:dpdown", ""},
    {"left", "D-pad left", "key:Left", "", "pad:dpleft", ""},
    {"right", "D-pad right", "key:Right", "", "pad:dpright", ""},
    {"left_stick_up", "Left stick up", "key:W", "", "axis:lefty-", ""},
    {"left_stick_down", "Left stick down", "key:S", "", "axis:lefty+", ""},
    {"left_stick_left", "Left stick left", "key:A", "", "axis:leftx-", ""},
    {"left_stick_right", "Left stick right", "key:D", "", "axis:leftx+", ""},
    {"right_stick_up", "Right stick up", "key:I", "", "axis:righty-", ""},
    {"right_stick_down", "Right stick down", "key:K", "", "axis:righty+", ""},
    {"right_stick_left", "Right stick left", "key:J", "", "axis:rightx-", ""},
    {"right_stick_right", "Right stick right", "key:L", "", "axis:rightx+", ""},
};

Settings g_settings = Defaults();
Settings g_started = g_settings;

std::string Trim(std::string_view text) {
    size_t begin = 0, end = text.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;

    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;

    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> SplitList(std::string_view text) {
    std::vector<std::string> items;
    size_t start = 0;

    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const auto item = Trim(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));

        if (!item.empty())
            items.push_back(item);

        if (comma == std::string_view::npos)
            break;

        start = comma + 1;
    }

    return items;
}

bool ParseBool(const std::string& value, bool& out) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return char(std::tolower(c)); });

    if (v == "on" || v == "true" || v == "yes" || v == "1") {
        out = true;
        return true;
    }

    if (v == "off" || v == "false" || v == "no" || v == "0") {
        out = false;
        return true;
    }

    return false;
}

bool ParseInt(const std::string& value, int& out) {
    char* end = nullptr;
    const long v = std::strtol(value.c_str(), &end, 10);

    if (value.empty() || *end != '\0')
        return false;

    out = int(v);
    return true;
}

bool ParseFloat(const std::string& value, float& out) {
    char* end = nullptr;
    const float v = std::strtof(value.c_str(), &end);

    if (value.empty() || *end != '\0')
        return false;

    out = v;
    return true;
}

bool ValidSource(const std::string& source) {
    if (source.empty())
        return true;

    const auto colon = source.find(':');

    if (colon == std::string::npos || colon + 1 == source.size())
        return false;

    const auto kind = source.substr(0, colon);

    if (kind == "axis") {
        const char sign = source.back();
        return source.size() > 6 && (sign == '+' || sign == '-');
    }

    return kind == "key" || kind == "mouse" || kind == "pad";
}

// "a, b" -> up to two sources; "none" clears.
bool ParseSources(const std::string& value, Sources& out) {
    Sources parsed{};

    if (value != "none") {
        // Sources may contain spaces ("key:Left Shift") but never "|".
        size_t start = 0, slot = 0;

        while (start <= value.size() && slot < parsed.size()) {
            const size_t bar = value.find('|', start);
            parsed[slot] = Trim(std::string_view(value).substr(start, bar == std::string::npos ? std::string::npos : bar - start));

            if (!ValidSource(parsed[slot]))
                return false;

            if (!parsed[slot].empty())
                ++slot;

            if (bar == std::string::npos)
                break;

            start = bar + 1;
        }
    }

    out = parsed;
    return true;
}

std::string SerializeSources(const Sources& sources) {
    std::string text;

    for (const auto& source : sources) {
        if (source.empty())
            continue;

        if (!text.empty())
            text += " | ";

        text += source;
    }

    return text.empty() ? "none" : text;
}

const char* DisplayModeName(DisplayMode mode) {
    switch (mode) {
    case DisplayMode::Borderless:
        return "borderless";
    case DisplayMode::Fullscreen:
        return "fullscreen";
    default:
        return "windowed";
    }
}

const char* FrameRateName(FrameRate rate) {
    switch (rate) {
    case FrameRate::Exact30:
        return "exact30";
    case FrameRate::Capped:
        return "capped";
    case FrameRate::Unlimited:
        return "unlimited";
    default:
        return "display";
    }
}

} // namespace

const char* ControlName(Control control) {
    return kControls[size_t(control)].name;
}

const char* ControlLabel(Control control) {
    return kControls[size_t(control)].label;
}

Settings Defaults() {
    Settings s;

    for (size_t i = 0; i < kControlCount; ++i) {
        s.keyboard[i] = {kControls[i].key0, kControls[i].key1};
        s.gamepad[i] = {kControls[i].pad0, kControls[i].pad1};
    }

    return s;
}

Settings Parse(std::string_view text, const Settings& base, std::vector<std::string>* warnings) {
    Settings s = base;
    // "mod" and "gecko" lines list one name each (names may hold commas):
    // the first such line replaces the base's list.
    bool modLines = false, geckoLines = false;
    std::istringstream input{std::string(text)};
    std::string line;
    int number = 0;

    const auto warn = [&](const std::string& message) {
        if (warnings)
            warnings->push_back("line " + std::to_string(number) + ": " + message);
    };

    while (std::getline(input, line)) {
        ++number;
        const auto stripped = Trim(line);

        if (stripped.empty() || stripped[0] == '#' || stripped[0] == ';')
            continue;

        const auto equals = stripped.find('=');

        if (equals == std::string::npos) {
            warn("expected key = value");
            continue;
        }

        const auto key = Trim(std::string_view(stripped).substr(0, equals));
        const auto value = Trim(std::string_view(stripped).substr(equals + 1));
        bool ok = true;
        int i = 0;
        float f = 0;
        bool b = false;

        if (key == "graphics.display_mode") {
            if (value == "windowed")
                s.displayMode = DisplayMode::Windowed;
            else if (value == "borderless")
                s.displayMode = DisplayMode::Borderless;
            else if (value == "fullscreen")
                s.displayMode = DisplayMode::Fullscreen;
            else
                ok = false;
        } else if (key == "graphics.resolution") {
            if (value == "auto")
                s.resolution = 0;
            else if (value == "window")
                s.resolution = -1;
            else if (ParseInt(value, i) && i >= 1 && i <= 8)
                s.resolution = i;
            else
                ok = false;
        } else if (key == "graphics.msaa") {
            ok = ParseInt(value, i) && (i == 1 || i == 2 || i == 4);
            if (ok)
                s.msaa = i;
        } else if (key == "graphics.anisotropy") {
            ok = ParseInt(value, i) && (i == 1 || i == 2 || i == 4 || i == 8 || i == 16);
            if (ok)
                s.anisotropy = i;
        } else if (key == "graphics.widescreen") {
            ok = ParseBool(value, b);
            if (ok)
                s.widescreen = b;
        } else if (key == "graphics.frame_rate") {
            if (value == "display")
                s.frameRate = FrameRate::Display;
            else if (value == "exact30")
                s.frameRate = FrameRate::Exact30;
            else if (value == "capped")
                s.frameRate = FrameRate::Capped;
            else if (value == "unlimited")
                s.frameRate = FrameRate::Unlimited;
            else
                ok = false;
        } else if (key == "graphics.fps_cap") {
            ok = ParseInt(value, i) && i >= 30 && i <= 360;
            if (ok)
                s.fpsCap = i;
        } else if (key == "graphics.show_fps") {
            ok = ParseBool(value, b);
            if (ok)
                s.showFps = b;
        } else if (key == "input.gamepad_device") {
            s.gamepadDevice = value == "any" ? "" : value;
        } else if (key == "input.dead_zone") {
            ok = ParseFloat(value, f) && f >= 0.f && f <= 0.5f;
            if (ok)
                s.deadZone = f;
        } else if (key == "input.mouse_look") {
            ok = ParseBool(value, b);
            if (ok)
                s.mouseLook = b;
        } else if (key == "input.mouse_sensitivity") {
            ok = ParseFloat(value, f) && f >= 0.1f && f <= 5.f;
            if (ok)
                s.mouseSensitivity = f;
        } else if (key == "input.invert_mouse_y") {
            ok = ParseBool(value, b);
            if (ok)
                s.invertMouseY = b;
        } else if (key == "input.wii_remotes") {
            ok = ParseBool(value, b);
            if (ok)
                s.wiiRemotes = b;
        } else if (key == "input.swap_sticks") {
            ok = ParseBool(value, b);
            if (ok)
                s.swapSticks = b;
        } else if (key.rfind("keyboard.", 0) == 0 || key.rfind("gamepad.", 0) == 0) {
            const bool keyboard = key[0] == 'k';
            const auto control = key.substr(keyboard ? 9 : 8);
            size_t index = kControlCount;

            for (size_t c = 0; c < kControlCount; ++c)
                if (control == kControls[c].name)
                    index = c;

            ok = index < kControlCount && ParseSources(value, keyboard ? s.keyboard[index] : s.gamepad[index]);
        } else if (key == "mod") {
            if (!std::exchange(modLines, true))
                s.enabledMods.clear();
            if (!value.empty())
                s.enabledMods.push_back(value);
        } else if (key == "mods.enabled") { // a comma-separated list
            s.enabledMods = SplitList(value);
        } else if (key == "textures.hd") {
            ok = ParseBool(value, b);
            if (ok)
                s.hdTextures = b;
        } else if (key == "textures.dump") {
            ok = ParseBool(value, b);
            if (ok)
                s.dumpTextures = b;
        } else if (key.rfind("cheat.", 0) == 0) {
            if (key.size() > 6)
                s.cheats[key.substr(6)] = value == "on" ? "" : value;
            else
                ok = false;
        } else if (key == "gecko") {
            if (!std::exchange(geckoLines, true))
                s.geckoCodes.clear();
            if (!value.empty())
                s.geckoCodes.push_back(value);
        } else if (key == "gecko.enabled") { // a comma-separated list
            s.geckoCodes = SplitList(value);
        } else if (key == "ui.scale") {
            ok = ParseFloat(value, f) && (f == 0.f || (f >= 0.5f && f <= 4.f));
            if (ok)
                s.uiScale = f;
        } else {
            warn("unknown setting " + key);
            continue;
        }

        if (!ok)
            warn("invalid value for " + key + ": " + value);
    }

    return s;
}

std::string Serialize(const Settings& s) {
    std::ostringstream out;
    out << "# Monster Hunter Tri (mh3-wii) PC settings. The in-game menu (F1, or L3+R3\n"
           "# on a gamepad) writes this file; edit it with the game closed.\n\n";
    out << "graphics.display_mode = " << DisplayModeName(s.displayMode) << "\n";
    out << "graphics.resolution = " << (s.resolution == 0 ? std::string("auto") : s.resolution < 0 ? std::string("window") : std::to_string(s.resolution)) << "\n";
    out << "graphics.msaa = " << s.msaa << "\n";
    out << "graphics.anisotropy = " << s.anisotropy << "\n";
    out << "graphics.widescreen = " << (s.widescreen ? "on" : "off") << "\n";
    out << "graphics.frame_rate = " << FrameRateName(s.frameRate) << "\n";
    out << "graphics.fps_cap = " << s.fpsCap << "\n";
    out << "graphics.show_fps = " << (s.showFps ? "on" : "off") << "\n\n";
    out << "input.gamepad_device = " << (s.gamepadDevice.empty() ? std::string("any") : s.gamepadDevice) << "\n";
    out << "input.dead_zone = " << s.deadZone << "\n";
    out << "input.mouse_look = " << (s.mouseLook ? "on" : "off") << "\n";
    out << "input.mouse_sensitivity = " << s.mouseSensitivity << "\n";
    out << "input.invert_mouse_y = " << (s.invertMouseY ? "on" : "off") << "\n";
    out << "input.wii_remotes = " << (s.wiiRemotes ? "on" : "off") << "\n";
    out << "input.swap_sticks = " << (s.swapSticks ? "on" : "off") << "\n\n";

    for (size_t c = 0; c < kControlCount; ++c)
        out << "keyboard." << kControls[c].name << " = " << SerializeSources(s.keyboard[c]) << "\n";

    out << "\n";

    for (size_t c = 0; c < kControlCount; ++c)
        out << "gamepad." << kControls[c].name << " = " << SerializeSources(s.gamepad[c]) << "\n";

    // Enabled mods in load order (later wins), one a line.
    out << "\n";

    for (const auto& mod : s.enabledMods)
        out << "mod = " << mod << "\n";

    out << "textures.hd = " << (s.hdTextures ? "on" : "off") << "\n";
    out << "textures.dump = " << (s.dumpTextures ? "on" : "off") << "\n";
    for (const auto& code : s.geckoCodes)
        out << "gecko = " << code << "\n";


    for (const auto& [name, value] : s.cheats)
        out << "cheat." << name << " = " << (value.empty() ? std::string("on") : value) << "\n";

    out << "\nui.scale = " << s.uiScale << "\n";
    return out.str();
}

Settings& Get() {
    return g_settings;
}

std::filesystem::path File() {
    const char* user = std::getenv("MH3_USER_DIR");
    return std::filesystem::path(user && *user ? user : "saves") / "settings.ini";
}

void Load() {
    std::ifstream file(File(), std::ios::binary);

    if (file) {
        std::stringstream text;
        text << file.rdbuf();
        std::vector<std::string> warnings;
        g_settings = Parse(text.str(), Defaults(), &warnings);

        for (const auto& warning : warnings)
            std::fprintf(stderr, "MH3 settings: %s: %s\n", File().string().c_str(), warning.c_str());
    } else
        g_settings = Defaults();

    MarkStarted();
}

bool Save() {
    const auto path = File();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    // Through a temporary file: a write cut short keeps the previous settings.
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << Serialize(g_settings);

        if (!out) {
            std::fprintf(stderr, "MH3 settings: cannot write %s\n", temporary.c_str());
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);

    if (error) {
        std::fprintf(stderr, "MH3 settings: cannot replace %s: %s\n", path.string().c_str(), error.message().c_str());
        return false;
    }

    return true;
}

bool RestartPending() {
    const auto& now = g_settings;
    const auto& was = g_started;
    return now.msaa != was.msaa || now.anisotropy != was.anisotropy || now.widescreen != was.widescreen || now.wiiRemotes != was.wiiRemotes ||
           now.enabledMods != was.enabledMods || now.hdTextures != was.hdTextures || now.dumpTextures != was.dumpTextures;
}

void MarkStarted() {
    g_started = g_settings;
}

} // namespace PcSettings
