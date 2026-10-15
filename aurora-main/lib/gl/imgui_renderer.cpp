// GLES3 renderer for the ImGui overlay (imgui_renderer.hpp). Deliberately
// self-contained: it creates its own program, buffers, vertex array and font
// texture, and puts back every piece of GL state it changes, so the
// renderer's own state cache (gl.cpp) stays valid.
#include "imgui_renderer.hpp"

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdio>

namespace aurora::gl {
namespace {

// ImDrawVert: ImVec2 pos, ImVec2 uv, ImU32 col (packed RGBA8, R in the low byte).
constexpr GLsizei kVertexStride = 20;

struct Objects {
  GLuint program = 0, vertexArray = 0, vertexBuffer = 0, indexBuffer = 0, fontTexture = 0;
  GLint projectionLocation = -1, textureLocation = -1;
  GLsizeiptr vertexCapacity = 0, indexCapacity = 0;
  uint32_t fontRevision = 0;
  bool failed = false;
} g;

GLuint compile(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

  if (ok != GL_TRUE) {
    char log[1024]{};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::fprintf(stderr, "aurora imgui overlay: shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

bool create_objects() {
  static const char* vertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform mat4 u_projection;
out vec2 v_uv;
out vec4 v_color;
void main() {
  v_uv = a_uv;
  v_color = a_color;
  gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
})";
  static const char* fragmentSource = R"(#version 300 es
precision mediump float;
uniform sampler2D u_texture;
in vec2 v_uv;
in vec4 v_color;
layout(location = 0) out vec4 o_color;
void main() {
  o_color = v_color * texture(u_texture, v_uv);
})";
  const GLuint vs = compile(GL_VERTEX_SHADER, vertexSource);
  const GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSource);

  if (vs == 0 || fs == 0) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return false;
  }

  g.program = glCreateProgram();
  glAttachShader(g.program, vs);
  glAttachShader(g.program, fs);
  glLinkProgram(g.program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = GL_FALSE;
  glGetProgramiv(g.program, GL_LINK_STATUS, &ok);

  if (ok != GL_TRUE) {
    std::fprintf(stderr, "aurora imgui overlay: program link failed\n");
    glDeleteProgram(g.program);
    g.program = 0;
    return false;
  }

  g.projectionLocation = glGetUniformLocation(g.program, "u_projection");
  g.textureLocation = glGetUniformLocation(g.program, "u_texture");
  glGenVertexArrays(1, &g.vertexArray);
  glGenBuffers(1, &g.vertexBuffer);
  glGenBuffers(1, &g.indexBuffer);
  glBindVertexArray(g.vertexArray);
  glBindBuffer(GL_ARRAY_BUFFER, g.vertexBuffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.indexBuffer);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, kVertexStride, reinterpret_cast<const void*>(0));
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kVertexStride, reinterpret_cast<const void*>(8));
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, kVertexStride, reinterpret_cast<const void*>(16));
  return true;
}

void upload_font(const imgui::FontAtlas& atlas) {
  if (g.fontTexture == 0)
    glGenTextures(1, &g.fontTexture);

  glBindTexture(GL_TEXTURE_2D, g.fontTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  GLint alignment = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(atlas.width), GLsizei(atlas.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
               atlas.rgba.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
  g.fontRevision = atlas.revision;
}

struct SavedState {
  GLint program, vertexArray, arrayBuffer, activeTexture, texture0, sampler0, viewport[4], scissorBox[4];
  GLint blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha, blendEqRgb, blendEqAlpha;
  GLboolean blend, cull, depth, stencil, scissor, colorMask[4], depthMask;
};

SavedState save_state() {
  SavedState s{};
  glGetIntegerv(GL_CURRENT_PROGRAM, &s.program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vertexArray);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuffer);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTexture);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.texture0);
  glGetIntegerv(GL_SAMPLER_BINDING, &s.sampler0);
  glGetIntegerv(GL_VIEWPORT, s.viewport);
  glGetIntegerv(GL_SCISSOR_BOX, s.scissorBox);
  glGetIntegerv(GL_BLEND_SRC_RGB, &s.blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &s.blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstAlpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &s.blendEqRgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.blendEqAlpha);
  s.blend = glIsEnabled(GL_BLEND);
  s.cull = glIsEnabled(GL_CULL_FACE);
  s.depth = glIsEnabled(GL_DEPTH_TEST);
  s.stencil = glIsEnabled(GL_STENCIL_TEST);
  s.scissor = glIsEnabled(GL_SCISSOR_TEST);
  glGetBooleanv(GL_COLOR_WRITEMASK, s.colorMask);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
  return s;
}

void set_enabled(GLenum cap, GLboolean on) {
  if (on)
    glEnable(cap);
  else
    glDisable(cap);
}

void restore_state(const SavedState& s) {
  glUseProgram(GLuint(s.program));
  glBindVertexArray(GLuint(s.vertexArray));
  glBindBuffer(GL_ARRAY_BUFFER, GLuint(s.arrayBuffer));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, GLuint(s.texture0));
  glBindSampler(0, GLuint(s.sampler0));
  glActiveTexture(GLenum(s.activeTexture));
  glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
  glScissor(s.scissorBox[0], s.scissorBox[1], s.scissorBox[2], s.scissorBox[3]);
  glBlendEquationSeparate(GLenum(s.blendEqRgb), GLenum(s.blendEqAlpha));
  glBlendFuncSeparate(GLenum(s.blendSrcRgb), GLenum(s.blendDstRgb), GLenum(s.blendSrcAlpha), GLenum(s.blendDstAlpha));
  set_enabled(GL_BLEND, s.blend);
  set_enabled(GL_CULL_FACE, s.cull);
  set_enabled(GL_DEPTH_TEST, s.depth);
  set_enabled(GL_STENCIL_TEST, s.stencil);
  set_enabled(GL_SCISSOR_TEST, s.scissor);
  glColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
  glDepthMask(s.depthMask);
}

} // namespace

void draw_imgui_overlay(uint32_t drawableWidth, uint32_t drawableHeight) noexcept {
  const auto snapshot = imgui::latest_snapshot();

  if (!snapshot || snapshot->lists.empty() || drawableWidth == 0 || drawableHeight == 0 || g.failed)
    return;

  const auto atlas = imgui::latest_font_atlas();

  if (!atlas || atlas->rgba.empty())
    return;

  const SavedState saved = save_state();

  if (g.program == 0 && !create_objects()) {
    g.failed = true;
    restore_state(saved);
    return;
  }

  if (g.fontTexture == 0 || g.fontRevision != atlas->revision)
    upload_font(*atlas);

  // The snapshot is in display units; the drawable may be larger (HiDPI).
  const float scaleX = snapshot->displaySize[0] > 0 ? float(drawableWidth) / snapshot->displaySize[0] : 1.f;
  const float scaleY = snapshot->displaySize[1] > 0 ? float(drawableHeight) / snapshot->displaySize[1] : 1.f;
  const float L = snapshot->displayPos[0], R = snapshot->displayPos[0] + snapshot->displaySize[0];
  const float T = snapshot->displayPos[1], B = snapshot->displayPos[1] + snapshot->displaySize[1];
  const float projection[16] = {
      2.f / (R - L), 0.f, 0.f, 0.f, 0.f, 2.f / (T - B), 0.f, 0.f, 0.f, 0.f, -1.f, 0.f, (R + L) / (L - R), (T + B) / (B - T), 0.f, 1.f,
  };

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(0, 0, GLsizei(drawableWidth), GLsizei(drawableHeight));
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_FALSE);
  glUseProgram(g.program);
  glUniformMatrix4fv(g.projectionLocation, 1, GL_FALSE, projection);
  glUniform1i(g.textureLocation, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g.fontTexture);
  glBindSampler(0, 0);
  glBindVertexArray(g.vertexArray);
  glBindBuffer(GL_ARRAY_BUFFER, g.vertexBuffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.indexBuffer);

  for (const auto& list : snapshot->lists) {
    const auto vertexBytes = GLsizeiptr(list.vertices.size());
    const auto indexBytes = GLsizeiptr(list.indices.size() * sizeof(uint16_t));

    if (vertexBytes > g.vertexCapacity) {
      g.vertexCapacity = vertexBytes * 2;
      glBufferData(GL_ARRAY_BUFFER, g.vertexCapacity, nullptr, GL_STREAM_DRAW);
    }

    if (indexBytes > g.indexCapacity) {
      g.indexCapacity = indexBytes * 2;
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, g.indexCapacity, nullptr, GL_STREAM_DRAW);
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexBytes, list.vertices.data());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indexBytes, list.indices.data());

    for (const auto& command : list.commands) {
      const float x1 = (command.clip[0] - L) * scaleX, y1 = (command.clip[1] - T) * scaleY;
      const float x2 = (command.clip[2] - L) * scaleX, y2 = (command.clip[3] - T) * scaleY;

      if (x2 <= x1 || y2 <= y1 || command.elementCount == 0)
        continue;

      // GL's scissor origin is the bottom left.
      glScissor(GLint(x1), GLint(float(drawableHeight) - y2), GLsizei(x2 - x1), GLsizei(y2 - y1));

      // GLES 3.0 has no base-vertex draw: vertex offsets are folded into the
      // indices when the snapshot is taken, so every list starts at vertex 0.
      glDrawElements(GL_TRIANGLES, GLsizei(command.elementCount), GL_UNSIGNED_SHORT,
                     reinterpret_cast<const void*>(uintptr_t(command.indexOffset) * sizeof(uint16_t)));
    }
  }

  restore_state(saved);
}

void shutdown_imgui_overlay() noexcept {
  if (g.fontTexture) glDeleteTextures(1, &g.fontTexture);
  if (g.vertexBuffer) glDeleteBuffers(1, &g.vertexBuffer);
  if (g.indexBuffer) glDeleteBuffers(1, &g.indexBuffer);
  if (g.vertexArray) glDeleteVertexArrays(1, &g.vertexArray);
  if (g.program) glDeleteProgram(g.program);
  g = {};
}

} // namespace aurora::gl
