/* NvkGui (ReaImGui fork)
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

#ifndef NVKGUI_FONT_REGISTRY_HPP
#define NVKGUI_FONT_REGISTRY_HPP

class Font;
struct ImFont;
struct ImFontAtlas;
struct ImTextureData;

// Extension-owned font atlas shared by every context that renders through a
// ProcessShared GPU device. Fonts dedupe by source, glyphs bake once per
// REAPER session, and the baked atlas (CPU and GPU side) survives script
// restarts: reopening a script skips rasterization and upload entirely.
namespace FontRegistry {
  // the shared atlas, or nullptr when the configured renderer cannot share
  // one GPU device across contexts (then the context owns a private atlas)
  ImFontAtlas *atlasForNewContext();
  bool isSharedAtlas(const ImFontAtlas *);
  bool ownsTexture(const ImTextureData *);

  ImFont *acquire(Font *); // dedupes by source; null if the font cannot load
  void release(ImFont *);  // dropping the last ref keeps the font baked

  void keepOwnersAlive(); // once per timer frame (Resource::Timer::tick)
  void newFrame();        // after all contexts ended their frame each tick
  void ensureFrameInit(); // after a context added its first shared font
  void teardown();        // plugin unload; after Resource::destroyAll,
                          // before RendererFactory::teardownSharedData
}

#endif
