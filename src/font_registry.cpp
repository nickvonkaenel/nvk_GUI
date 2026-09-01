/* nvk_GUI (ReaImGui fork)
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

#include "font_registry.hpp"

#include "font.hpp"
#include "renderer.hpp"
#include "settings.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <typeinfo>
#include <vector>

struct Entry {
  // The first Font resource to install this source. Kept alive (never
  // garbage-collected) because the shared ImFont's fallback resolution
  // state and ImFontConfig::UserData live on it.
  Font *owner;
  ImFont *instance;
  unsigned int refs;
};

static std::vector<Entry> g_entries;
static ImFontAtlas *g_atlas;
static int g_frameCount;

ImFontAtlas *FontRegistry::atlasForNewContext()
{
  // Sharing one atlas requires one GPU device consuming its texture updates
  // for every rendering context; only ProcessShared backends provide that
  // (OpenGL objects belong to a platform share group, not the process).
  if(!Settings::Renderer ||
      !(Settings::Renderer->flags & RendererType::ProcessShared))
    return nullptr;

  if(!g_atlas) {
    g_atlas = IM_NEW(ImFontAtlas);
    g_atlas->Flags |= ImFontAtlasFlags_NoMouseCursors;
    g_atlas->SetFontLoader(Font::loader());
  }
  return g_atlas;
}

// A context-owned atlas is frame-updated by ImGui::NewFrame; ours must be
// updated by hand (imgui asserts a context never runs a frame on an atlas
// whose builder was not initialized and kept current). The update may only
// run once the atlas holds at least one font: building an empty atlas
// falls back to AddFontDefault, which this build disables.
static bool canUpdateAtlas()
{
  return g_atlas && g_atlas->Fonts.Size > 0;
}

void FontRegistry::ensureFrameInit()
{
  if(canUpdateAtlas() &&
      (!g_atlas->Builder || g_atlas->Builder->FrameCount == -1))
    ImFontAtlasUpdateNewFrame(g_atlas, ++g_frameCount, true);
}

void FontRegistry::newFrame()
{
  // Called from Resource::Timer::tick after every context rendered and
  // ended its frame: baked-glyph GC and ImFontBaked cache clearing must
  // never run while a sharing context is mid-frame.
  if(canUpdateAtlas())
    ImFontAtlasUpdateNewFrame(g_atlas, ++g_frameCount, true);
}

bool FontRegistry::isSharedAtlas(const ImFontAtlas *atlas)
{
  return g_atlas && atlas == g_atlas;
}

bool FontRegistry::ownsTexture(const ImTextureData *tex)
{
  if(!g_atlas)
    return false;
  for(const ImTextureData *candidate : g_atlas->TexList) {
    if(candidate == tex)
      return true;
  }
  return false;
}

// Same source and legacy size, and same dynamic type: a SysFont and a plain
// Font can carry equal FontSources (CreateFontFromFile strips the
// SystemFallback flag before storing) yet must not share an instance.
static bool matches(const Font *a, const Font *b)
{
  return typeid(*a) == typeid(*b) && a->source() == b->source() &&
    a->legacySize() == b->legacySize();
}

ImFont *FontRegistry::acquire(Font *font)
{
  for(Entry &entry : g_entries) {
    if(matches(entry.owner, font)) {
      ++entry.refs;
      return entry.instance;
    }
  }

  ImFont *instance {font->source().install(g_atlas, font)};
  if(!instance)
    return nullptr;
  instance->LegacySize = font->legacySize();
  g_entries.push_back({font, instance, 1});
  return instance;
}

void FontRegistry::release(ImFont *instance)
{
  for(Entry &entry : g_entries) {
    if(entry.instance == instance) {
      if(entry.refs)
        --entry.refs;
      return; // zero refs keeps the font baked for future contexts
    }
  }
}

void FontRegistry::keepOwnersAlive()
{
  for(const Entry &entry : g_entries)
    entry.owner->keepAlive();
}

void FontRegistry::teardown()
{
  // the owners were Resources: Resource::destroyAll already deleted them
  g_entries.clear();

  if(!g_atlas)
    return;

  for(ImTextureData *tex : g_atlas->TexList) {
    if(tex->GetTexID() != ImTextureID_Invalid)
      RendererFactory::destroyTextureOnTeardown(tex);
  }
  IM_DELETE(g_atlas);
  g_atlas = nullptr;
}
