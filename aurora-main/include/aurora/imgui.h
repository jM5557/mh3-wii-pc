#ifndef AURORA_IMGUI_H
#define AURORA_IMGUI_H

#include <imgui.h>

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include "stdint.h"
#include "stdbool.h"
#endif

// An overlay UI drawn over the presented frame (lib/imgui.cpp). Call on the
// thread that polls events, after aurora_update: begin_frame returns false
// when no overlay can be built (no window yet). Between begin and end, use
// the ImGui API; end publishes the frame, which the GLES renderer draws over
// every presented image until the next end_frame or aurora_imgui_clear.
bool aurora_imgui_begin_frame(void);
void aurora_imgui_end_frame(void);

// Stop drawing the overlay (until the next end_frame).
void aurora_imgui_clear(void);

// After adding or changing fonts (ImGui::GetIO().Fonts) outside a frame.
void aurora_imgui_rebuild_fonts(void);

// Whether the embedder's UI currently owns input (for its own bookkeeping).
void aurora_imgui_set_capture_input(bool capture);

ImTextureID aurora_imgui_add_texture(uint32_t width, uint32_t height, const void* rgba8);

#ifdef __cplusplus
}
#endif

#endif
