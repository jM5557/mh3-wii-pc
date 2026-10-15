// Dear ImGui for embedders' overlays (aurora/imgui.h).
//
// The UI is built on the thread that polls events (the game thread): SDL
// events reach ImGui through imgui_impl_sdl3 in process_event, and the
// embedder brackets its widgets with aurora_imgui_begin_frame /
// aurora_imgui_end_frame. end_frame copies the frame's draw data into an
// immutable snapshot; the GLES render thread draws the latest snapshot over
// the presented image (gl/imgui_renderer.cpp). Nothing on the render thread
// touches the ImGui context. On renderers without an overlay (GXM, WebGPU)
// frames are built and dropped.
#include "imgui.hpp"

#include "gl/imgui_renderer.hpp"
#include "window.hpp"

#include <aurora/imgui.h>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

// Compiled into the imgui_backends library (extern/CMakeLists.txt).
#include <backends/imgui_impl_sdl3.h>

namespace aurora::imgui {
namespace {
std::mutex g_publishMutex;
std::shared_ptr<const Snapshot> g_snapshot;
std::shared_ptr<const FontAtlas> g_atlas;
uint32_t g_atlasRevision = 0;
bool g_backendReady = false;
bool g_inFrame = false;
std::atomic<bool> g_captureInput{false};

void publish_atlas() {
  ImGuiIO& io = ImGui::GetIO();
  unsigned char* pixels = nullptr;
  int width = 0, height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  if (pixels == nullptr || width <= 0 || height <= 0)
    return;

  auto atlas = std::make_shared<FontAtlas>();
  atlas->width = uint32_t(width);
  atlas->height = uint32_t(height);
  atlas->revision = ++g_atlasRevision;
  atlas->rgba.assign(pixels, pixels + size_t(width) * size_t(height) * 4);
  // Every draw command samples the atlas; the id only has to be non-zero.
  io.Fonts->SetTexID(ImTextureID(1));
  std::lock_guard lock(g_publishMutex);
  g_atlas = std::move(atlas);
}

std::shared_ptr<const Snapshot> take_snapshot(const ImDrawData* data) {
  if (data == nullptr || data->CmdListsCount == 0 || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0)
    return nullptr;

  auto snapshot = std::make_shared<Snapshot>();
  snapshot->displayPos[0] = data->DisplayPos.x;
  snapshot->displayPos[1] = data->DisplayPos.y;
  snapshot->displaySize[0] = data->DisplaySize.x;
  snapshot->displaySize[1] = data->DisplaySize.y;
  snapshot->framebufferScale[0] = data->FramebufferScale.x;
  snapshot->framebufferScale[1] = data->FramebufferScale.y;
  snapshot->lists.reserve(size_t(data->CmdListsCount));

  for (const ImDrawList* drawList : data->CmdLists) {
    SnapshotList list;
    static_assert(sizeof(ImDrawVert) == 20, "the overlay renderer expects ImGui's default vertex layout");
    static_assert(sizeof(ImDrawIdx) == 2, "the overlay renderer expects 16-bit ImGui indices");
    list.vertices.resize(size_t(drawList->VtxBuffer.Size) * sizeof(ImDrawVert));
    std::memcpy(list.vertices.data(), drawList->VtxBuffer.Data, list.vertices.size());
    list.indices.assign(drawList->IdxBuffer.Data, drawList->IdxBuffer.Data + drawList->IdxBuffer.Size);

    for (const ImDrawCmd& cmd : drawList->CmdBuffer) {
      if (cmd.UserCallback != nullptr)
        continue; // no callbacks cross threads

      SnapshotCommand command;
      command.clip[0] = cmd.ClipRect.x;
      command.clip[1] = cmd.ClipRect.y;
      command.clip[2] = cmd.ClipRect.z;
      command.clip[3] = cmd.ClipRect.w;
      command.elementCount = cmd.ElemCount;
      command.indexOffset = cmd.IdxOffset;
      command.vertexOffset = cmd.VtxOffset; // 0: RendererHasVtxOffset is not set
      list.commands.push_back(command);
    }

    snapshot->lists.push_back(std::move(list));
  }

  return snapshot;
}
} // namespace

std::shared_ptr<const Snapshot> latest_snapshot() noexcept {
  std::lock_guard lock(g_publishMutex);
  return g_snapshot;
}

std::shared_ptr<const FontAtlas> latest_font_atlas() noexcept {
  std::lock_guard lock(g_publishMutex);
  return g_atlas;
}

void create_context() noexcept {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
}

void initialize() noexcept {
  if (ImGui::GetCurrentContext() == nullptr || window::get_sdl_window() == nullptr)
    return;

  g_backendReady = ImGui_ImplSDL3_InitForOther(window::get_sdl_window());
}

void shutdown() noexcept {
  if (g_backendReady)
    ImGui_ImplSDL3_Shutdown();

  g_backendReady = false;
  {
    std::lock_guard lock(g_publishMutex);
    g_snapshot.reset();
    g_atlas.reset();
  }

  if (ImGui::GetCurrentContext() != nullptr)
    ImGui::DestroyContext();
}

void process_event(const SDL_Event& event) noexcept {
  if (g_backendReady && ImGui::GetCurrentContext() != nullptr)
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool wants_capture_event(const SDL_Event&) noexcept { return g_captureInput.load(std::memory_order_relaxed); }

void new_frame(const AuroraWindowSize&) noexcept {}
} // namespace aurora::imgui

using namespace aurora::imgui;

bool aurora_imgui_begin_frame(void) {
  if (!g_backendReady || ImGui::GetCurrentContext() == nullptr || g_inFrame)
    return false;

  ImGuiIO& io = ImGui::GetIO();

  // The embedder may add fonts before the first frame; the atlas is built
  // and published once (and again after aurora_imgui_rebuild_fonts).
  if (!io.Fonts->IsBuilt() || latest_font_atlas() == nullptr)
    publish_atlas();

  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  g_inFrame = true;
  return true;
}

void aurora_imgui_end_frame(void) {
  if (!g_inFrame)
    return;

  g_inFrame = false;
  ImGui::Render();
  auto snapshot = take_snapshot(ImGui::GetDrawData());
  std::lock_guard lock(g_publishMutex);
  g_snapshot = std::move(snapshot);
}

void aurora_imgui_clear(void) {
  std::lock_guard lock(g_publishMutex);
  g_snapshot.reset();
}

void aurora_imgui_rebuild_fonts(void) {
  if (ImGui::GetCurrentContext() == nullptr || g_inFrame)
    return;

  ImGui::GetIO().Fonts->Build();
  publish_atlas();
}

void aurora_imgui_set_capture_input(bool capture) { g_captureInput.store(capture, std::memory_order_relaxed); }

ImTextureID aurora_imgui_add_texture(uint32_t, uint32_t, const void*) {
  // Only the font atlas is drawn; images are not supported by the overlay.
  return ImTextureID(1);
}
