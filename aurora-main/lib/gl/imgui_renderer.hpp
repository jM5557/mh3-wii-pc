#pragma once

// Draws a Dear ImGui frame snapshot over the presented image on the GLES
// render thread (aurora::gl::present, after the scene's blit, before the swap).
// The UI itself is built on the game thread (lib/imgui.cpp); only this copied
// snapshot crosses threads, so the render thread never touches the ImGui
// context.

#include <cstdint>
#include <memory>
#include <vector>

namespace aurora::imgui {

struct SnapshotCommand {
  float clip[4];             // x1, y1, x2, y2 in display coordinates
  uint32_t elementCount = 0; // indices
  uint32_t indexOffset = 0;  // into the list's indices
  uint32_t vertexOffset = 0; // added to each index
};

// One ImDrawList's output. Vertices are ImDrawVert (pos.xy, uv.xy, col RGBA8).
struct SnapshotList {
  std::vector<uint8_t> vertices;
  std::vector<uint16_t> indices;
  std::vector<SnapshotCommand> commands;
};

struct Snapshot {
  float displayPos[2]{};
  float displaySize[2]{};
  float framebufferScale[2]{1.f, 1.f};
  std::vector<SnapshotList> lists;
};

// The font atlas, RGBA8. Copied once on the game thread; the render thread
// uploads it on first use (and again when `revision` changes).
struct FontAtlas {
  uint32_t width = 0, height = 0, revision = 0;
  std::vector<uint8_t> rgba;
};

// The latest published frame (null: nothing to draw) and atlas.
std::shared_ptr<const Snapshot> latest_snapshot() noexcept;
std::shared_ptr<const FontAtlas> latest_font_atlas() noexcept;

} // namespace aurora::imgui

namespace aurora::gl {
// Render thread: draw the latest snapshot into the bound default framebuffer
// of drawableWidth x drawableHeight pixels. Restores the GL state it touches.
void draw_imgui_overlay(uint32_t drawableWidth, uint32_t drawableHeight) noexcept;
// Render thread: free the overlay's GL objects (context shutdown).
void shutdown_imgui_overlay() noexcept;
} // namespace aurora::gl
