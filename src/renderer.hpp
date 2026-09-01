/* ReaImGui: ReaScript binding for Dear ImGui
 * Copyright (C) 2021-2025  Christian Fillion
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef REAIMGUI_RENDERER_HPP
#define REAIMGUI_RENDERER_HPP

#include <array>
#include <memory>

class Renderer;
class RendererFactory;
class Window;
struct ImTextureData;
struct ImVec2;
struct ImVec4;

struct RendererType {
  enum Flags {
    Available        = 1<<0,
    CanForceSoftware = 1<<1,
    // The backend's shared block holds only process-wide GPU objects
    // (device, compiled shaders, pipeline state), so one instance can serve
    // every context and outlive the last window. Not safe for OpenGL, whose
    // objects belong to a platform share group.
    ProcessShared    = 1<<2,
  };

  struct Register {
    Register(RendererType *);
  };

  static const RendererType *bestMatch(const char *id);
  static const RendererType *head();

  char priority, flags;
  const char *id, *name, *displayName;
  std::unique_ptr<Renderer>(*creator)(RendererFactory *, Window *);
  RendererType *next;
};

class RendererFactory {
public:
  RendererFactory();

  const char *name() const { return m_type->name; }
  std::unique_ptr<Renderer> create(Window *);

  // GPU-frees one texture on a ProcessShared backend's device
  using TextureSweeper = void (*)(void *sharedData, ImTextureData *);

  template<typename T>
  auto getSharedData() const { return std::static_pointer_cast<T>(getSharedDataErased()); }

  template<typename T>
  void setSharedData(T d, TextureSweeper sweeper = nullptr)
  { setSharedDataErased(std::move(d), sweeper); }

  // GPU-frees one of the current context's textures. Called from Context
  // teardown: a ProcessShared block outlives the last window, so its
  // destructor can no longer sweep on the dying context's behalf.
  void destroyTexture(ImTextureData *) const;
  // last-resort sweep at plugin unload for textures no context owns anymore
  static void destroyTextureOnTeardown(ImTextureData *);

  // releases the extension-held ProcessShared GPU blocks (plugin unload)
  static void teardownSharedData();

  bool wantSoftware() const { return m_forceSoftware; }

private:
  std::shared_ptr<void> getSharedDataErased() const;
  void setSharedDataErased(std::shared_ptr<void>, TextureSweeper);

  const RendererType *m_type;
  std::weak_ptr<void> m_shared;
  bool m_forceSoftware;
};

class Renderer {
public:
  static void install();

  template<typename T>
  static std::unique_ptr<Renderer> create(RendererFactory *factory, Window *window)
  {
    return std::make_unique<T>(factory, window);
  }

  Renderer(Window *);
  virtual ~Renderer();

  virtual void setSize(ImVec2) = 0;
  virtual void render(void *) = 0;
  virtual void swapBuffers(void *) = 0;

protected:
  class ProjMtx {
  public:
    ProjMtx(const ImVec2 &pos, const ImVec2 &size, bool flip = false);
    const float *operator&() const { return &m_data[0][0]; }

  private:
    std::array<std::array<float, 4>, 4> m_data;
  };
  static_assert(sizeof(ProjMtx) == sizeof(float[4][4]));

  struct ClipRect {
    ClipRect(const ImVec4 &rect, const ImVec2 &offset, const ImVec2 &scale);
    operator bool() const;
    long left, top, right, bottom;
  };

  Window *m_window;
};

#define REGISTER_RENDERER(priority, id, name, creator, flags)   \
  static RendererType rendererType_##id                         \
    {priority, flags, #id, "reaper_imgui_" #id, name, creator}; \
  RendererType::Register regRenderer_##id {&rendererType_##id};

#endif
