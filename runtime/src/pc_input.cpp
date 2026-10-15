// Bound host controls to the Classic Controller (pc_input.h).
#include "pc_input.h"
#include "pc_settings.h"
#include "../native/mh3_classic_map.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace PcInput {
namespace {

bool blocked = false;
SDL_Gamepad* pad = nullptr;
std::string padGuid;

// Rebinding capture.
bool capturing = false, captureKeyboard = true;
std::vector<uint8_t> keysBefore;
SDL_MouseButtonFlags mouseBefore = 0;
std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> padButtonsBefore{};
std::array<int, SDL_GAMEPAD_AXIS_COUNT> padAxesBefore{}; // -1, 0, +1 past the capture threshold
bool comboHeld = false;

constexpr float kCaptureAxis = 0.6f;

std::string GuidString(SDL_GUID guid) {
    char text[64]{};
    SDL_GUIDToString(guid, text, sizeof(text));
    return text;
}

bool EnsureGamepadSubsystem() {
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return false;

    return SDL_WasInit(SDL_INIT_GAMEPAD) || SDL_InitSubSystem(SDL_INIT_GAMEPAD);
}

// The chosen gamepad when connected, else the first; reopened on hotplug.
SDL_Gamepad* ActivePad() {
    const auto& wanted = PcSettings::Get().gamepadDevice;

    if (pad && (!SDL_GamepadConnected(pad) || (!wanted.empty() && padGuid != wanted))) {
        SDL_CloseGamepad(pad);
        pad = nullptr;
        padGuid.clear();
    }

    if (pad || !EnsureGamepadSubsystem())
        return pad;

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_JoystickID chosen = 0;

    for (int i = 0; ids && i < count && !chosen; ++i)
        if (!wanted.empty() && GuidString(SDL_GetGamepadGUIDForID(ids[i])) == wanted)
            chosen = ids[i];

    if (!chosen && ids && count)
        chosen = ids[0];

    if (chosen) {
        pad = SDL_OpenGamepad(chosen);
        padGuid = pad ? GuidString(SDL_GetGamepadGUIDForID(chosen)) : "";
    }

    SDL_free(ids);
    return pad;
}

float AxisValue(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) {
    return std::clamp(SDL_GetGamepadAxis(gamepad, axis) / 32767.0f, -1.0f, 1.0f);
}

SDL_MouseButtonFlags MouseFlag(std::string_view name) {
    if (name == "left")
        return SDL_BUTTON_LMASK;
    if (name == "right")
        return SDL_BUTTON_RMASK;
    if (name == "middle")
        return SDL_BUTTON_MMASK;
    if (name == "x1")
        return SDL_BUTTON_X1MASK;
    if (name == "x2")
        return SDL_BUTTON_X2MASK;
    return 0;
}

// One source's value, 0..1.
float SourceValue(const std::string& source, const bool* keys, SDL_MouseButtonFlags mouse, SDL_Gamepad* gamepad) {
    const auto colon = source.find(':');

    if (colon == std::string::npos)
        return 0.f;

    const std::string_view kind(source.data(), colon);
    const std::string name = source.substr(colon + 1);

    if (kind == "key") {
        const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
        return keys && code != SDL_SCANCODE_UNKNOWN && keys[code] ? 1.f : 0.f;
    }

    if (kind == "mouse")
        return (mouse & MouseFlag(name)) ? 1.f : 0.f;

    if (!gamepad)
        return 0.f;

    if (kind == "pad") {
        const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());
        return button != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(gamepad, button) ? 1.f : 0.f;
    }

    if (kind == "axis" && name.size() > 1) {
        const float sign = name.back() == '-' ? -1.f : 1.f;
        const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name.substr(0, name.size() - 1).c_str());

        if (axis == SDL_GAMEPAD_AXIS_INVALID)
            return 0.f;

        // Triggers rest at 0 (SDL3); sticks are signed.
        return std::max(0.f, sign * AxisValue(gamepad, axis));
    }

    return 0.f;
}

using PcSettings::Control;

float ControlValue(Control control, const bool* keys, SDL_MouseButtonFlags mouse, SDL_Gamepad* gamepad) {
    const auto& settings = PcSettings::Get();
    float value = 0.f;

    if (keys || mouse)
        for (const auto& source : settings.keyboard[size_t(control)])
            value = std::max(value, SourceValue(source, keys, mouse, nullptr));

    if (gamepad)
        for (const auto& source : settings.gamepad[size_t(control)])
            value = std::max(value, SourceValue(source, nullptr, 0, gamepad));

    return value;
}

struct ButtonControl {
    Control control;
    uint16_t bit;
};

constexpr ButtonControl kButtons[] = {
    {Control::A, MH3_CL_A},       {Control::B, MH3_CL_B},         {Control::X, MH3_CL_X},       {Control::Y, MH3_CL_Y},
    {Control::ZL, MH3_CL_ZL},     {Control::ZR, MH3_CL_ZR},       {Control::Plus, MH3_CL_PLUS}, {Control::Minus, MH3_CL_MINUS},
    {Control::Home, MH3_CL_HOME}, {Control::Up, MH3_CL_UP},       {Control::Down, MH3_CL_DOWN}, {Control::Left, MH3_CL_LEFT},
    {Control::Right, MH3_CL_RIGHT},
};

void Stick(float x, float y, int16_t& outX, int16_t& outY) {
    mh3_stick_curve curve = mh3_gamepad_stick_curve;
    curve.dead_zone = PcSettings::Get().deadZone;
    mh3_classic_stick(x, y, &curve, &outX, &outY);
}

} // namespace

void SetBlocked(bool value) {
    blocked = value;
}

bool Blocked() {
    return blocked;
}

ClassicControllerSample Read() {
    ClassicControllerSample sample;
    sample.connected = true;

    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return sample;

    SDL_Gamepad* gamepad = ActivePad();

    if (blocked)
        return sample;

    const bool* keys = SDL_GetKeyboardFocus() ? SDL_GetKeyboardState(nullptr) : nullptr;
    const SDL_MouseButtonFlags mouse = SDL_GetMouseFocus() ? SDL_GetMouseState(nullptr, nullptr) : 0;

    for (const auto& button : kButtons)
        if (ControlValue(button.control, keys, mouse, gamepad) > 0.5f)
            sample.buttons |= button.bit;

    // The Classic's digital L/R close at the end of the trigger's travel;
    // half way is kinder to worn gamepad triggers. A digital source is a full press.
    const float l = ControlValue(Control::L, keys, mouse, gamepad), r = ControlValue(Control::R, keys, mouse, gamepad);
    sample.triggerL = uint8_t(std::lround(l * 255));
    sample.triggerR = uint8_t(std::lround(r * 255));

    if (sample.triggerL > 127)
        sample.buttons |= MH3_CL_L;

    if (sample.triggerR > 127)
        sample.buttons |= MH3_CL_R;

    // Sticks: +x right, +y up.
    float lx = ControlValue(Control::LeftStickRight, keys, mouse, gamepad) - ControlValue(Control::LeftStickLeft, keys, mouse, gamepad);
    float ly = ControlValue(Control::LeftStickUp, keys, mouse, gamepad) - ControlValue(Control::LeftStickDown, keys, mouse, gamepad);
    float rx = ControlValue(Control::RightStickRight, keys, mouse, gamepad) - ControlValue(Control::RightStickLeft, keys, mouse, gamepad);
    float ry = ControlValue(Control::RightStickUp, keys, mouse, gamepad) - ControlValue(Control::RightStickDown, keys, mouse, gamepad);
    const auto& settings = PcSettings::Get();

    // Mouse look: the motion since the last read as right-stick deflection.
    if (settings.mouseLook && SDL_GetWindowRelativeMouseMode(SDL_GetMouseFocus())) {
        float dx = 0, dy = 0;
        SDL_GetRelativeMouseState(&dx, &dy);
        rx += dx * settings.mouseSensitivity * 0.08f;
        ry += (settings.invertMouseY ? dy : -dy) * settings.mouseSensitivity * 0.08f;
    }

    if (settings.swapSticks) {
        std::swap(lx, rx);
        std::swap(ly, ry);
    }

    Stick(std::clamp(lx, -1.f, 1.f), std::clamp(ly, -1.f, 1.f), sample.leftX, sample.leftY);
    Stick(std::clamp(rx, -1.f, 1.f), std::clamp(ry, -1.f, 1.f), sample.rightX, sample.rightY);
    return sample;
}

std::vector<Gamepad> Gamepads() {
    std::vector<Gamepad> result;

    if (!EnsureGamepadSubsystem())
        return result;

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);

    for (int i = 0; ids && i < count; ++i) {
        const char* name = SDL_GetGamepadNameForID(ids[i]);
        result.push_back({name ? name : "Gamepad", GuidString(SDL_GetGamepadGUIDForID(ids[i]))});
    }

    SDL_free(ids);
    return result;
}

std::string ActiveGamepadName() {
    SDL_Gamepad* gamepad = ActivePad();
    const char* name = gamepad ? SDL_GetGamepadName(gamepad) : nullptr;
    return name ? name : "";
}

void BeginCapture(bool keyboard) {
    capturing = true;
    captureKeyboard = keyboard;
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    keysBefore.assign(keys, keys + count);
    mouseBefore = SDL_GetMouseState(nullptr, nullptr);
    SDL_Gamepad* gamepad = ActivePad();

    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
        padButtonsBefore[size_t(b)] = gamepad && SDL_GetGamepadButton(gamepad, SDL_GamepadButton(b));

    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
        const float v = gamepad ? AxisValue(gamepad, SDL_GamepadAxis(a)) : 0.f;
        padAxesBefore[size_t(a)] = v > kCaptureAxis ? 1 : v < -kCaptureAxis ? -1 : 0;
    }
}

bool Capturing() {
    return capturing;
}

void CancelCapture() {
    capturing = false;
}

std::optional<std::string> PollCapture() {
    if (!capturing)
        return std::nullopt;

    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);

    // Escape cancels a capture (it stays bindable from the file).
    if (keys[SDL_SCANCODE_ESCAPE] && !(SDL_SCANCODE_ESCAPE < int(keysBefore.size()) && keysBefore[SDL_SCANCODE_ESCAPE])) {
        capturing = false;
        return std::string();
    }

    std::optional<std::string> found;

    if (captureKeyboard) {
        for (int code = 0; code < count && !found; ++code)
            if (keys[code] && !(code < int(keysBefore.size()) && keysBefore[size_t(code)]) && code != SDL_SCANCODE_F1)
                if (const char* name = SDL_GetScancodeName(SDL_Scancode(code)); name && *name)
                    found = std::string("key:") + name;

        const SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);
        static constexpr std::pair<SDL_MouseButtonFlags, const char*> buttons[] = {
            {SDL_BUTTON_RMASK, "right"}, {SDL_BUTTON_MMASK, "middle"}, {SDL_BUTTON_X1MASK, "x1"}, {SDL_BUTTON_X2MASK, "x2"}, {SDL_BUTTON_LMASK, "left"},
        };

        for (const auto& [flag, name] : buttons)
            if (!found && (mouse & flag) && !(mouseBefore & flag))
                found = std::string("mouse:") + name;

        mouseBefore = mouse;
        keysBefore.assign(keys, keys + count);
    } else if (SDL_Gamepad* gamepad = ActivePad()) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && !found; ++b) {
            const bool down = SDL_GetGamepadButton(gamepad, SDL_GamepadButton(b));

            if (down && !padButtonsBefore[size_t(b)])
                if (const char* name = SDL_GetGamepadStringForButton(SDL_GamepadButton(b)))
                    found = std::string("pad:") + name;

            padButtonsBefore[size_t(b)] = down;
        }

        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT && !found; ++a) {
            const float v = AxisValue(gamepad, SDL_GamepadAxis(a));
            const int now = v > kCaptureAxis ? 1 : v < -kCaptureAxis ? -1 : 0;

            if (now && now != padAxesBefore[size_t(a)])
                if (const char* name = SDL_GetGamepadStringForAxis(SDL_GamepadAxis(a)))
                    found = std::string("axis:") + name + (now > 0 ? "+" : "-");

            padAxesBefore[size_t(a)] = now;
        }
    }

    if (found)
        capturing = false;

    return found;
}

std::string SourceLabel(const std::string& source) {
    if (source.empty())
        return "-";

    const auto colon = source.find(':');
    const std::string kind = source.substr(0, colon), name = colon == std::string::npos ? "" : source.substr(colon + 1);

    if (kind == "key")
        return name;

    if (kind == "mouse")
        return "Mouse " + name;

    if (kind == "pad") {
        static constexpr std::pair<const char*, const char*> labels[] = {
            {"a", "A / south"}, {"b", "B / east"}, {"x", "X / west"}, {"y", "Y / north"}, {"back", "Back / Select"}, {"guide", "Guide / Home"},
            {"start", "Start"}, {"leftstick", "L3"}, {"rightstick", "R3"}, {"leftshoulder", "LB / L1"}, {"rightshoulder", "RB / R1"},
            {"dpup", "D-pad up"}, {"dpdown", "D-pad down"}, {"dpleft", "D-pad left"}, {"dpright", "D-pad right"},
        };

        for (const auto& [id, label] : labels)
            if (name == id)
                return label;

        return "Pad " + name;
    }

    if (kind == "axis" && !name.empty()) {
        const std::string axis = name.substr(0, name.size() - 1);
        const char sign = name.back();

        if (axis == "lefttrigger")
            return "LT / L2";
        if (axis == "righttrigger")
            return "RT / R2";
        if (axis == "leftx")
            return sign == '+' ? "Left stick right" : "Left stick left";
        if (axis == "lefty")
            return sign == '+' ? "Left stick down" : "Left stick up";
        if (axis == "rightx")
            return sign == '+' ? "Right stick right" : "Right stick left";
        if (axis == "righty")
            return sign == '+' ? "Right stick down" : "Right stick up";

        return "Axis " + name;
    }

    return source;
}

bool MenuComboPressed() {
    if (!EnsureGamepadSubsystem())
        return false;

    bool held = false;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);

    for (int i = 0; ids && i < count && !held; ++i)
        if (SDL_Gamepad* gamepad = SDL_GetGamepadFromID(ids[i]))
            held = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK) && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);

    SDL_free(ids);

    // Gamepads nobody opened report nothing: the active one is always open.
    if (!held)
        if (SDL_Gamepad* gamepad = ActivePad())
            held = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK) && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);

    const bool pressed = held && !comboHeld;
    comboHeld = held;
    return pressed;
}

void SetMouseCaptured(bool want) {
    SDL_Window* window = SDL_GetKeyboardFocus();

    if (!window)
        return;

    if (SDL_GetWindowRelativeMouseMode(window) != want) {
        SDL_SetWindowRelativeMouseMode(window, want);

        if (want) {
            float dx, dy;
            SDL_GetRelativeMouseState(&dx, &dy); // drop the motion before the capture
        }
    }
}

} // namespace PcInput
