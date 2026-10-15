// The PC in-game menu (pc_menu.h).
#include "pc_menu.h"

#include "frame_limit.h"
#include "gecko.h"
#include "launcher_cheats.h"
#include "mods.h"
#include "msaa.h"
#include "pc_graphics.h"
#include "pc_input.h"
#include "pc_paths.h"
#include "pc_settings.h"
#include "widescreen.h"

#include <aurora/imgui.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace PcMenu {
namespace {

using Clock = std::chrono::steady_clock;
using PcSettings::Control;

bool open = false;
bool closeRequested = false; // the window's close button
bool dirty = false; // settings changed since the last save
Clock::time_point lastSave;
Clock::time_point started = Clock::now();
bool hintShown = false;

// The tab the gamepad's shoulder buttons moved to (-1: none requested).
int currentTab = 0, requestTab = -1;
constexpr int kTabCount = 5;

// Rebinding: the control and column being captured.
int captureControl = -1, captureSlot = 0;
bool captureKeyboard = true;
Clock::time_point captureStart;

// Fonts are built for this scale.
float fontScale = 0.f;

// Mods found under mods/ (refreshed when the menu opens).
std::vector<Mods::Info> mods;

// The FPS counter: frames and time over the last half second.
double fpsShown = 0, msShown = 0;
int fpsFrames = 0;
Clock::time_point fpsStart = Clock::now(), lastFrame = Clock::now();
double frameMsSum = 0;

PcSettings::Settings& S() {
    return PcSettings::Get();
}

void Changed() {
    dirty = true;
}

void SaveIfDirty(bool now) {
    if (dirty && (now || Clock::now() - lastSave > std::chrono::seconds(1))) {
        PcSettings::Save();
        dirty = false;
        lastSave = Clock::now();
    }
}

// ---- Fonts ----

// A system UI font when one is installed (sharper when scaled), else ImGui's
// built-in one. MH3_UI_FONT names a .ttf to use instead.
std::string FindFont() {
    if (const char* font = std::getenv("MH3_UI_FONT"); font && *font)
        return font;

    static const char* const candidates[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",                     // Arch
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",         // Debian, Ubuntu
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",       // Fedora
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",              // Arch, SteamOS
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",     // Debian
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",       // Fedora
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    std::error_code error;

    for (const char* path : candidates)
        if (std::filesystem::is_regular_file(path, error))
            return path;

    return {};
}

float WantedScale() {
    if (S().uiScale > 0.f)
        return std::clamp(S().uiScale, 0.5f, 4.f);

    // Readable from a couch: 1 at 720 lines, the Steam Deck's 800 lines 1.1.
    const float height = ImGui::GetIO().DisplaySize.y;
    return height > 0 ? std::clamp(std::round(height / 720.f * 4.f) / 4.f, 1.f, 3.f) : 1.f;
}

void BuildFonts(float scale) {
    static const std::string path = FindFont();
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFont* font = nullptr;

    if (!path.empty())
        font = io.Fonts->AddFontFromFileTTF(path.c_str(), std::round(17.f * scale));

    if (!font) {
        ImFontConfig config;
        config.SizePixels = std::round(13.f * scale);
        io.Fonts->AddFontDefault(&config);
    }

    aurora_imgui_rebuild_fonts();
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 6.f;
    style.FrameRounding = 4.f;
    style.GrabRounding = 4.f;
    style.TabRounding = 4.f;
    style.WindowBorderSize = 0.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.10f, 0.94f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.20f, 0.08f, 1.f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.45f, 0.30f, 0.10f, 0.55f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.60f, 0.40f, 0.12f, 0.80f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.70f, 0.47f, 0.15f, 1.f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.45f, 0.30f, 0.10f, 0.55f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.40f, 0.12f, 0.85f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.47f, 0.15f, 1.f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.16f, 0.13f, 0.90f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.24f, 0.15f, 0.90f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.30f, 0.15f, 0.90f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.75f, 0.30f, 1.f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.62f, 0.22f, 1.f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.75f, 0.30f, 1.f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.25f, 0.18f, 0.08f, 0.86f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.60f, 0.40f, 0.12f, 0.80f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.55f, 0.36f, 0.11f, 1.f);
    style.Colors[ImGuiCol_NavCursor] = ImVec4(1.00f, 0.80f, 0.35f, 1.f);
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
    fontScale = scale;
}

// ---- Small widgets ----

void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("%s", text);
}

template <typename T>
bool Combo(const char* label, T& value, std::initializer_list<std::pair<T, const char*>> choices) {
    const char* current = "?";

    for (const auto& [v, text] : choices)
        if (v == value)
            current = text;

    bool changed = false;

    if (ImGui::BeginCombo(label, current)) {
        for (const auto& [v, text] : choices) {
            const bool selected = v == value;

            if (ImGui::Selectable(text, selected) && !selected) {
                value = v;
                changed = true;
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }

    if (changed)
        Changed();

    return changed;
}

bool Check(const char* label, bool& value) {
    if (ImGui::Checkbox(label, &value)) {
        Changed();
        return true;
    }

    return false;
}

void RestartNote() {
    ImGui::SameLine();
    ImGui::TextDisabled("(restart)");
    ImGui::SetItemTooltip("Takes effect the next time the game starts.");
}

void OpenFolder(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    const auto url = "file://" + std::filesystem::absolute(path, error).generic_string();
    SDL_OpenURL(url.c_str());
}

// "Label [Open] [extra] path": the buttons stay in view however long the
// path is. Returns whether `extra` (a second button) was pressed.
bool PathLine(const char* label, const std::filesystem::path& path, const char* extra = nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::PushID(label);

    if (ImGui::SmallButton("Open"))
        OpenFolder(path);

    bool pressed = false;

    if (extra) {
        ImGui::SameLine();
        pressed = ImGui::SmallButton(extra);
    }

    ImGui::PopID();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", path.string().c_str());
    ImGui::SetItemTooltip("%s", path.string().c_str());
    return pressed;
}

// ---- Tabs ----

void GraphicsTab() {
    auto& s = S();
    ImGui::SeparatorText("Display");

    if (Combo<PcSettings::DisplayMode>("Display mode", s.displayMode,
                                       {{PcSettings::DisplayMode::Windowed, "Windowed"},
                                        {PcSettings::DisplayMode::Borderless, "Borderless fullscreen"},
                                        {PcSettings::DisplayMode::Fullscreen, "Exclusive fullscreen"}}))
        PcApplyDisplayMode();

    Help("Alt+Enter also toggles fullscreen.");

    if (Combo<int>("Render resolution", s.resolution,
                   {{0, "Default (960x544 surface)"},
                    {-1, "Match window"},
                    {1, "1x Wii (640x448)"},
                    {2, "2x Wii (1280x896)"},
                    {3, "3x Wii (1920x1344)"},
                    {4, "4x Wii (2560x1792)"},
                    {5, "5x Wii"},
                    {6, "6x Wii"},
                    {8, "8x Wii"}}))
        PcApplyRenderResolution();

    Help("The size the game renders at, scaled to the window. \"Match window\" renders one pixel per window pixel.");
    Combo<int>("Anti-aliasing", s.msaa, {{1, "Off"}, {2, "2x MSAA"}, {4, "4x MSAA"}});
    RestartNote();
    Combo<int>("Texture filtering", s.anisotropy, {{1, "Trilinear"}, {2, "2x anisotropic"}, {4, "4x anisotropic"}, {8, "8x anisotropic"}, {16, "16x anisotropic"}});
    RestartNote();
    Check("Widescreen (16:9)", s.widescreen);
    RestartNote();

    ImGui::SeparatorText("Frame rate");
    Combo<PcSettings::FrameRate>("Frame rate", s.frameRate,
                                 {{PcSettings::FrameRate::Display, "Unlocked (display rate)"},
                                  {PcSettings::FrameRate::Capped, "Unlocked, capped"},
                                  {PcSettings::FrameRate::Unlimited, "Unlimited"},
                                  {PcSettings::FrameRate::Exact30, "30 fps (original timing)"}});
    Help("Unlocked: the game advances by each frame's real time, so it runs smoothly at any rate. "
         "Most actions keep the Wii's speed; a few timers counted in frames can still differ (report them!).\n\n"
         "30 fps (original timing): the game's own fixed 30 Hz steps. Every action runs exactly as on the Wii.");

    if (s.frameRate == PcSettings::FrameRate::Capped) {
        if (ImGui::SliderInt("FPS cap", &s.fpsCap, 30, 360, "%d fps"))
            Changed();
    }

    Check("Show FPS", s.showFps);

    ImGui::SeparatorText("Menu");
    float scale = s.uiScale;

    if (ImGui::SliderFloat("Menu size", &scale, 0.f, 3.f, scale <= 0.f ? "Auto" : "%.2fx")) {
        s.uiScale = scale < 0.5f ? 0.f : scale;
        Changed();
    }
}

void BindingCell(int control, bool keyboard, int slot) {
    auto& sources = keyboard ? S().keyboard[size_t(control)] : S().gamepad[size_t(control)];
    const bool capturing = captureControl == control && captureKeyboard == keyboard && captureSlot == slot;
    std::string label = capturing ? (keyboard ? "Press a key..." : "Press a button...") : PcInput::SourceLabel(sources[size_t(slot)]);
    label += "##" + std::to_string(control) + (keyboard ? "k" : "p") + std::to_string(slot);

    if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0)) && !PcInput::Capturing()) {
        captureControl = control;
        captureKeyboard = keyboard;
        captureSlot = slot;
        captureStart = Clock::now();
        PcInput::BeginCapture(keyboard);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft)))) {
        sources[size_t(slot)].clear();
        Changed();
    }

    ImGui::SetItemTooltip("Select to rebind. Right-click, Delete or X/Square clears.");
}

void PollRebinding() {
    if (captureControl < 0)
        return;

    auto result = PcInput::PollCapture();

    // A gamepad has no Escape: a capture with no press ends after 5 seconds.
    if (!result && Clock::now() - captureStart > std::chrono::seconds(5)) {
        PcInput::CancelCapture();
        result = std::string();
    }

    if (!result)
        return;

    if (!result->empty()) {
        auto& table = captureKeyboard ? S().keyboard : S().gamepad;

        // One source drives one control: take it from wherever it was.
        for (auto& sources : table)
            for (auto& source : sources)
                if (source == *result)
                    source.clear();

        table[size_t(captureControl)][size_t(captureSlot)] = *result;
        Changed();
    }

    captureControl = -1;
}

void ControlsTab() {
    auto& s = S();
    ImGui::SeparatorText("Gamepad");
    const auto pads = PcInput::Gamepads();
    std::string current = "First connected";

    for (const auto& pad : pads)
        if (pad.guid == s.gamepadDevice)
            current = pad.name;

    if (!s.gamepadDevice.empty() && current == "First connected")
        current = "Saved gamepad (not connected)";

    if (ImGui::BeginCombo("Device", current.c_str())) {
        if (ImGui::Selectable("First connected", s.gamepadDevice.empty())) {
            s.gamepadDevice.clear();
            Changed();
        }

        for (size_t i = 0; i < pads.size(); ++i) {
            ImGui::PushID(int(i));

            if (ImGui::Selectable(pads[i].name.c_str(), pads[i].guid == s.gamepadDevice)) {
                s.gamepadDevice = pads[i].guid;
                Changed();
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    const auto active = PcInput::ActiveGamepadName();
    ImGui::TextDisabled("In use: %s  (%zu connected)", active.empty() ? "none" : active.c_str(), pads.size());

    if (ImGui::SliderFloat("Stick dead zone", &s.deadZone, 0.f, 0.5f, "%.2f"))
        Changed();

    Check("Swap sticks", s.swapSticks);
    Check("Wii Remotes (Bluetooth)", s.wiiRemotes);
    RestartNote();
    Help("Pairs Wii Remotes through SDL's Wii driver. A Wii Remote with a Classic Controller, or a Nunchuk, "
         "shows up as a gamepad and can be chosen and bound above. Pointer (IR) and motion aren't used.");

    ImGui::SeparatorText("Mouse");
    Check("Mouse look (camera)", s.mouseLook);
    Help("Mouse movement turns the camera, as the right stick. The cursor is captured while the menu is closed.");

    if (s.mouseLook) {
        if (ImGui::SliderFloat("Sensitivity", &s.mouseSensitivity, 0.1f, 5.f, "%.2f"))
            Changed();

        Check("Invert vertical", s.invertMouseY);
    }

    ImGui::SeparatorText("Bindings (Classic Controller)");

    if (ImGui::Button("Reset keyboard")) {
        s.keyboard = PcSettings::Defaults().keyboard;
        Changed();
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset gamepad")) {
        s.gamepad = PcSettings::Defaults().gamepad;
        Changed();
    }

    if (captureControl >= 0)
        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.35f, 1.f), captureKeyboard ? "Press a key or mouse button for %s (Escape cancels)" : "Press a button or move a stick for %s (5 s)",
                           PcSettings::ControlLabel(Control(captureControl)));

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("bindings", 5, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("Keyboard 2", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("Gamepad 2", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableHeadersRow();

        for (int c = 0; c < int(PcSettings::kControlCount); ++c) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(PcSettings::ControlLabel(Control(c)));

            for (int column = 0; column < 4; ++column) {
                ImGui::TableNextColumn();
                BindingCell(c, column < 2, column % 2);
            }
        }

        ImGui::EndTable();
    }
}

void ModsTab() {
    auto& s = S();
    ImGui::SeparatorText("Mods");
    ImGui::TextWrapped("Each folder in mods/ replaces game files by their disc path (as in a Dolphin extraction, "
                       "with or without files/). Later mods in the list win. Changes apply at the next start.");
    if (PathLine("Folder:", PcPaths::Mods(), "Refresh"))
        mods = Mods::Discover(PcPaths::Mods());

    if (mods.empty())
        ImGui::TextDisabled("No mods found.");

    // Enabled mods in load order, then the rest.
    std::vector<const Mods::Info*> ordered;

    for (const auto& name : s.enabledMods)
        for (const auto& mod : mods)
            if (mod.folder == name)
                ordered.push_back(&mod);

    for (const auto& mod : mods)
        if (std::find(s.enabledMods.begin(), s.enabledMods.end(), mod.folder) == s.enabledMods.end())
            ordered.push_back(&mod);

    for (size_t i = 0; i < ordered.size(); ++i) {
        const auto& mod = *ordered[i];
        ImGui::PushID(mod.folder.c_str());
        const auto at = std::find(s.enabledMods.begin(), s.enabledMods.end(), mod.folder);
        bool enabled = at != s.enabledMods.end();

        if (ImGui::Checkbox("##on", &enabled)) {
            if (enabled)
                s.enabledMods.push_back(mod.folder);
            else
                s.enabledMods.erase(at);

            Changed();
        }

        ImGui::SameLine();
        const auto position = size_t(at - s.enabledMods.begin());

        ImGui::BeginDisabled(!enabled || position == 0);
        if (ImGui::ArrowButton("up", ImGuiDir_Up)) {
            std::swap(s.enabledMods[position], s.enabledMods[position - 1]);
            Changed();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!enabled || position + 1 >= s.enabledMods.size());
        if (ImGui::ArrowButton("down", ImGuiDir_Down)) {
            std::swap(s.enabledMods[position], s.enabledMods[position + 1]);
            Changed();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s", mod.title.c_str());

        if (!mod.version.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mod.version.c_str());
        }

        if (!mod.author.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("by %s", mod.author.c_str());
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(%zu files, %.1f MB)", mod.files, double(mod.bytes) / (1 << 20));

        if (!mod.description.empty())
            ImGui::SetItemTooltip("%s", mod.description.c_str());

        ImGui::PopID();
    }

    for (const auto& name : s.enabledMods)
        if (std::none_of(mods.begin(), mods.end(), [&](const Mods::Info& mod) { return mod.folder == name; }))
            ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "Enabled mod \"%s\" is missing from mods/", name.c_str());

    ImGui::SeparatorText("HD textures");
    Check("Load HD texture packs", s.hdTextures);
    RestartNote();
    Help("Dolphin-format packs (tex1_WxH_hash_format.dds/.png, any folder depth) for Monster Hunter Tri PAL, "
         "game ID RMHP08. DDS may be uncompressed or BC1/BC2/BC3/BC4/BC5/BC7, with or without mipmaps.");
    PathLine("Folder:", PcPaths::Textures());
    Check("Dump textures (for making packs)", s.dumpTextures);
    RestartNote();
    Help("Writes each texture the game shows, once, under Dump/Textures/RMHP08 with the name a pack uses.");
    PathLine("Dumps:", PcPaths::TextureDumps());
}

void CheatsTab() {
    auto& s = S();
    const auto cheats = LauncherCheats::List();
    ImGui::TextWrapped("Native versions of the published code lists. They apply at once and are remembered.");

    if (ImGui::Button("Turn all off")) {
        for (size_t i = 0; i < cheats.size(); ++i)
            LauncherCheats::Enable(i, false);

        s.cheats.clear();
        Changed();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%zu on", LauncherCheats::EnabledCount());

    if (ImGui::BeginChild("cheats", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.6f), ImGuiChildFlags_Borders)) {
        for (const char* group : LauncherCheats::Groups()) {
            if (!ImGui::CollapsingHeader(group))
                continue;

            for (size_t i = 0; i < cheats.size(); ++i) {
                const auto& cheat = cheats[i];

                if (std::string_view(cheat.group) != group)
                    continue;

                ImGui::PushID(int(i));
                const std::string key = std::string(cheat.group) + ":" + cheat.name;
                bool enabled = LauncherCheats::Enabled(i);
                bool update = false;

                if (ImGui::Checkbox(cheat.name, &enabled)) {
                    LauncherCheats::Enable(i, enabled);
                    update = true;
                }

                if (cheat.detail && *cheat.detail)
                    ImGui::SetItemTooltip("%s", cheat.detail);

                if (const size_t choices = LauncherCheats::Choices(i)) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);

                    if (ImGui::BeginCombo("##value", LauncherCheats::ValueLabel(i).c_str())) {
                        for (size_t c = 0; c < choices; ++c) {
                            const size_t before = LauncherCheats::Selected(i);
                            LauncherCheats::Select(i, c);
                            const auto text = LauncherCheats::ValueLabel(i);
                            LauncherCheats::Select(i, before);

                            if (ImGui::Selectable(text.c_str(), c == before)) {
                                LauncherCheats::Select(i, c);
                                update = true;
                            }
                        }

                        ImGui::EndCombo();
                    }
                } else if (const size_t length = LauncherCheats::TextLength(i)) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
                    char text[64] = {};
                    const auto current = LauncherCheats::Text(i);
                    std::snprintf(text, sizeof(text), "%s", current.c_str());

                    if (ImGui::InputText("##text", text, std::min(sizeof(text), length + 1)) && LauncherCheats::SetText(i, text))
                        update = true;
                }

                if (update) {
                    if (LauncherCheats::Enabled(i))
                        s.cheats[key] = LauncherCheats::TextLength(i) ? LauncherCheats::Text(i) : LauncherCheats::Choices(i) ? LauncherCheats::ValueLabel(i) : "";
                    else
                        s.cheats.erase(key);

                    Changed();
                }

                ImGui::PopID();
            }
        }
    }

    ImGui::EndChild();

    ImGui::SeparatorText("Gecko codes");
    if (PathLine("Folder:", PcPaths::Codes(), "Reload"))
        Gecko::LoadFiles();

    Help("Dolphin game-settings files (RMHP08.ini, [Gecko] section) or .txt lists. RAM write, condition and pointer codes run each frame; "
         "codes that patch game instructions can't work in a recompilation and are greyed out (most have a native version above).");
    const auto& codes = Gecko::Loaded();

    if (codes.empty())
        ImGui::TextDisabled("No Gecko codes found.");

    if (ImGui::BeginChild("gecko", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (size_t i = 0; i < codes.size(); ++i) {
            const auto& code = codes[i];
            ImGui::PushID(int(i));
            const auto at = std::find(s.geckoCodes.begin(), s.geckoCodes.end(), code.name);
            bool enabled = at != s.geckoCodes.end();
            ImGui::BeginDisabled(!code.unsupported.empty());

            if (ImGui::Checkbox(code.name.c_str(), &enabled)) {
                if (enabled)
                    s.geckoCodes.push_back(code.name);
                else
                    s.geckoCodes.erase(at);

                Changed();
            }

            ImGui::EndDisabled();
            std::string tip = code.author.empty() ? "" : "By " + code.author + "\n";
            tip += code.notes;

            if (!code.unsupported.empty())
                tip += (tip.empty() ? "" : "\n") + std::string("Unsupported: ") + code.unsupported;

            if (!tip.empty())
                ImGui::SetItemTooltip("%s", tip.c_str());

            ImGui::PopID();
        }
    }

    ImGui::EndChild();
}

void AboutTab() {
    ImGui::Text("Monster Hunter Tri - PC recompilation");
    ImGui::TextDisabled("Built %s", __DATE__);
    ImGui::SeparatorText("Menu controls");
    ImGui::BulletText("F1, or L3 + R3 on a gamepad: open or close this menu");
    ImGui::BulletText("Mouse, or arrows / Enter / Escape, or d-pad / A / B");
    ImGui::BulletText("Q / E or LB / RB: previous / next tab");
    ImGui::BulletText("Alt+Enter: toggle fullscreen");
    ImGui::SeparatorText("Folders");
    PathLine("Data:", PcPaths::Data());
    PathLine("Mods:", PcPaths::Mods());
    PathLine("HD textures:", PcPaths::Textures());
    PathLine("Gecko codes:", PcPaths::Codes());
    ImGui::TextUnformatted("Settings:");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", PcSettings::File().string().c_str());
}

void MenuWindow() {
    const auto& io = ImGui::GetIO();
    const ImVec2 size(std::min(io.DisplaySize.x * 0.92f, 980.f * fontScale), io.DisplaySize.y * 0.88f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    bool keep = true;

    if (ImGui::Begin("Settings###pcmenu", &keep, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        if (PcSettings::RestartPending())
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.35f, 1.f), "Some changes take effect the next time the game starts.");

        // Shoulder buttons (and Q/E) change tabs, unless a rebinding is waiting for them.
        if (captureControl < 0 && !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false))
                requestTab = (currentTab + kTabCount - 1) % kTabCount;

            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_E, false))
                requestTab = (currentTab + 1) % kTabCount;
        }

        if (ImGui::BeginTabBar("tabs")) {
            static const char* const names[kTabCount] = {"Graphics", "Controls", "Mods & textures", "Cheats", "About"};
            static void (*const tabs[kTabCount])() = {GraphicsTab, ControlsTab, ModsTab, CheatsTab, AboutTab};
            const int requested = requestTab;
            requestTab = -1;

            for (int t = 0; t < kTabCount; ++t)
                if (ImGui::BeginTabItem(names[t], nullptr, requested == t ? ImGuiTabItemFlags_SetSelected : 0)) {
                    currentTab = t;

                    if (ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NavFlattened)) {
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
                        tabs[t]();
                        ImGui::PopItemWidth();
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();

    if (!keep)
        closeRequested = true;
}

void FpsOverlay() {
    if (!S().showFps)
        return;

    ImGui::SetNextWindowPos(ImVec2(8.f * fontScale, 8.f * fontScale));
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("%.0f fps  %.1f ms", fpsShown, msShown);
    ImGui::End();
}

void Hint() {
    if (hintShown)
        return;

    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();

    if (seconds > 10.0) {
        hintShown = true;
        return;
    }

    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 8.f * fontScale, io.DisplaySize.y - 8.f * fontScale), ImGuiCond_Always, ImVec2(1.f, 1.f));
    ImGui::SetNextWindowBgAlpha(float(std::clamp(10.0 - seconds, 0.0, 1.0)) * 0.6f);
    ImGui::Begin("##hint", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("F1 or L3+R3: settings");
    ImGui::End();
}

void CountFrame() {
    const auto now = Clock::now();
    frameMsSum += std::chrono::duration<double, std::milli>(now - lastFrame).count();
    lastFrame = now;
    ++fpsFrames;
    const double elapsed = std::chrono::duration<double>(now - fpsStart).count();

    if (elapsed >= 0.5) {
        fpsShown = fpsFrames / elapsed;
        msShown = frameMsSum / fpsFrames;
        fpsFrames = 0;
        frameMsSum = 0;
        fpsStart = now;
    }
}

void SetOpen(bool value) {
    if (open == value)
        return;

    open = value;

    if (open) {
        mods = Mods::Discover(PcPaths::Mods());
        hintShown = true;
    } else {
        if (captureControl >= 0) {
            PcInput::CancelCapture();
            captureControl = -1;
        }

        SaveIfDirty(true);
    }
}

} // namespace

void ApplyStartupSettings() {
    PcSettings::Load();
    const auto& s = PcSettings::Get();
    SetMsaaSetting(uint32_t(s.msaa));
    // MH3_WIDESCREEN (docs/flags.md) still wins for a run that sets it.
    if (const char* v = std::getenv("MH3_WIDESCREEN"); !v || !*v)
        SetWidescreenSetting(s.widescreen);
    SetFrameLimitSetting(s.frameRate != PcSettings::FrameRate::Unlimited);

    // SDL's Wii Remote driver is off by default; it must be on before SDL starts.
    if (s.wiiRemotes) {
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII_PLAYER_LED, "1");
    }

    for (const auto& [name, value] : s.cheats)
        if (!LauncherCheats::EnableByName(name, value))
            std::fprintf(stderr, "MH3 settings: unknown cheat %s = %s; skipped\n", name.c_str(), value.c_str());

    Gecko::LoadFiles();
    PcSettings::MarkStarted();
}

bool Open() {
    return open;
}

void Frame() {
    CountFrame();
    Gecko::RunEnabled();

    if (const float scale = WantedScale(); scale != fontScale)
        BuildFonts(scale);

    if (!aurora_imgui_begin_frame())
        return;

    auto& io = ImGui::GetIO();
    const bool capturing = captureControl >= 0;
    bool toggle = PcInput::MenuComboPressed() || ImGui::IsKeyPressed(ImGuiKey_F1, false);

    // Escape closes the menu, unless it is cancelling a rebinding or leaving
    // a text field or popup.
    if (open && !capturing && !io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        toggle = true;

    if (toggle)
        SetOpen(!open);

    // A rebinding reads the raw devices: the menu's navigation stays out of it.
    if (capturing)
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    else
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    if (open) {
        PollRebinding();
        MenuWindow();

        if (closeRequested) {
            closeRequested = false;
            SetOpen(false);
        }
    }

    io.MouseDrawCursor = false;
    FpsOverlay();
    Hint();
    aurora_imgui_end_frame();

    PcInput::SetBlocked(open);
    PcInput::SetMouseCaptured(!open && S().mouseLook);
    aurora_imgui_set_capture_input(open);
    SaveIfDirty(!open);
}

} // namespace PcMenu
