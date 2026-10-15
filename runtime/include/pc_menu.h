#pragma once
// The PC build's in-game menu (Dear ImGui over the presented frame): display
// and frame-rate settings, control bindings, mods, HD textures, cheats and
// Gecko codes. F1, or L3 + R3 on a gamepad, opens and closes it; it works
// with the mouse, the keyboard (arrows, Enter, Escape) or a gamepad (d-pad,
// A to choose, B to go back, LB/RB to change tabs). While it is open the game
// gets no input. Settings save to settings.ini (pc_settings.h).
namespace PcMenu {

// Loads settings.ini and applies what the game reads at startup (MSAA,
// widescreen, the frame limit, Wii Remotes, saved cheats, Gecko code files).
// Call once in main, before the graphics start and before command-line
// cheats (which then add to the saved ones).
void ApplyStartupSettings();

// Once per game frame on the game thread (frame_pacer.cpp): the menu, the
// FPS counter and the enabled Gecko codes.
void Frame();

bool Open();

} // namespace PcMenu
