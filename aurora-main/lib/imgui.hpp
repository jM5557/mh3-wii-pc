#pragma once

#include <aurora/event.h>

union SDL_Event;

// The ImGui context behind aurora/imgui.h: input through imgui_impl_sdl3 on
// the event thread, frames drawn by the GLES renderer (gl/imgui_renderer.cpp).
namespace aurora::imgui {
void create_context() noexcept;
void initialize() noexcept;
void shutdown() noexcept;

void process_event(const SDL_Event& event) noexcept;
bool wants_capture_event(const SDL_Event& event) noexcept;
void new_frame(const AuroraWindowSize& size) noexcept;
} // namespace aurora::imgui
