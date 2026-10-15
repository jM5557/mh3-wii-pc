#pragma once
// The PC menu's graphics settings applied to the running renderer
// (graphics_host.cpp). Game thread.

// The render target's size from PcSettings::resolution.
void PcApplyRenderResolution();

// Windowed, borderless or exclusive fullscreen from PcSettings::displayMode.
void PcApplyDisplayMode();
