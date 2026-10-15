#pragma once
// Host controls to the Classic Controller through the bindings in
// PcSettings (keyboard, mouse buttons, mouse look, a chosen SDL gamepad), and
// the helpers the menu's rebinding uses. SDL only; game thread.
#include "controller_host.h"

#include <optional>
#include <string>
#include <vector>

namespace PcInput {

// The live sample: every bound source, sticks through the dead zone curve.
// While Blocked (the menu is open) the sample is neutral but connected.
ClassicControllerSample Read();

void SetBlocked(bool blocked);
bool Blocked();

struct Gamepad {
    std::string name;
    std::string guid; // SDL_GUIDToString of the gamepad's GUID
};

// Connected gamepads, in SDL's order.
std::vector<Gamepad> Gamepads();
// The gamepad the game reads (the chosen one when connected, else the first),
// or an empty name.
std::string ActiveGamepadName();

// Rebinding: Begin, then Poll each frame until it returns a source (or
// Escape cancels: Poll returns "" once). keyboard selects keys and mouse
// buttons, else gamepad buttons and axes.
void BeginCapture(bool keyboard);
std::optional<std::string> PollCapture();
bool Capturing();
void CancelCapture();

// A source for people: "Space", "Mouse right", "B / east", "Left trigger".
std::string SourceLabel(const std::string& source);

// L3 and R3 newly held together on any gamepad (the menu's toggle).
bool MenuComboPressed();

// Relative mouse mode for mouse look: on while `want`, off otherwise.
void SetMouseCaptured(bool want);

} // namespace PcInput
