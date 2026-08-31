# ReaImGui Fork Plan & Codebase Findings

Handoff document for the project of forking ReaImGui into a **renamed, coexisting
extension** optimized for our own REAPER scripts. Everything below was derived from
reading this codebase (upstream `master` at commit `b3466c1`, Dear ImGui 1.92.1
vendored). Performance numbers are **code-derived estimates, not profiles** — see
"Measurements to take" at the end.

---

## 1. Goal and key decisions

- **Goal**: streamline and optimize ReaImGui for *our* scripts only. Not trying to
  improve the ecosystem or replace stock ReaImGui.
- **Decision: rename, don't replace.** The fork registers all API functions under a
  new prefix (e.g. `NvkGui_` instead of `ImGui_`) and ships as a differently-named
  DLL, so it **coexists** with stock ReaImGui on the same machine. Users keep stock
  (via ReaPack) for third-party scripts; our scripts explicitly depend on the fork.
  - This removes the single biggest risk of a "drop-in replacement" fork: owning API
    compatibility for third-party scripts and fighting ReaPack over the install channel.
  - It also frees us to break/extend the API at will (only our scripts call it), and
    we could even strip the version-shim machinery (`ShimFunc`, `shims/` handling in
    `src/api.cpp`) since we control all callers.
- **Trade-off accepted**: third-party scripts get no benefit; when stock and fork run
  simultaneously, GPU/atlas resources are duplicated across the two extensions (same
  as today, just partitioned).
- **Structure the fork as**: a mechanical "rebrand" patch series as the *first*
  commits, followed by functional patches, rebased onto upstream tags. Upstream
  (cfillion, active maintainer) tracks Dear ImGui releases; rebases will conflict most
  in resource lifetime / renderer code. Upstream anything upstream will take (see §7).

## 2. Uncommitted local changes already in this working tree

`git diff` shows an existing local patch (not yet committed anywhere):

- **`src/docker.cpp`**: adds `collectNodeWindows`/`mergeNodeWindows`. Changes
  `Docker::moveTo` and `Docker::hostViewport` so that moving windows into an
  already-occupied REAPER docker **merges into its existing layout (as tabs)**
  instead of undocking all contained windows. Comment notes REAPER's docker tabs
  merge the same way. `hostViewport` no longer asserts `!isActive()`.
- **`src/window.cpp`**: removes the context-menu code that disabled selecting a
  docker that is already in use (consistent with the merge behavior above).

This patch is a good candidate to **upstream as a PR** (behavioral fix, not
fork-specific). Don't lose it when setting up the fork.

## 3. Architecture facts that shape everything (verified, with locations)

### Contexts are fully isolated worlds
- One `ImGui.CreateContext()` per script = one `Context` (`src/context.cpp:112`)
  containing its own ImGui context, **its own font atlas** (FreeType loader +
  default `SysFont{SANS_SERIF}` set at `src/context.cpp:120,172-173`), its own
  `DockerList`, and its own `RendererFactory`.
- **Each context creates its own D3D10 device + shaders + pipeline state.** The
  `D3D10Renderer::Shared` block (device, DXGI factory, compiled shaders, blend/
  raster/sampler states) is shared only *between windows of one context* via
  `RendererFactory::get/setSharedData` (a `weak_ptr`, `src/d3d10_renderer.cpp:291-294`).
  N running scripts = N GPU devices. Same pattern for Metal/OpenGL backends.
- **GPU textures are per-context**: the atlas texture(s) and every image.
  `Image::install` calls `ctx->createTexture()` (`src/image.cpp:224`); textures live
  in `m_imgui->UserTextures` (`src/context.cpp:624-645`). The same PNG attached to
  3 scripts is in VRAM 3 times.

### Resource lifetime / GC (src/resource.cpp)
- All script-visible objects (Context, Font, Image, …) are `Resource`s in a global
  `FlatSet g_rsx`. A REAPER timer (`Resource::Timer::tick`) runs every defer frame
  and deletes any resource whose `m_keepAlive` counter expired.
- `KEEP_ALIVE_FRAMES = 2` (`src/resource.cpp:35`): a resource dies **2 timer frames
  after the script stops touching it**. Script exit ⇒ its context and fonts die.
  Restart ⇒ reload font file, re-rasterize, re-upload. Nothing survives restarts.
- `Context::attach()` (max 0x400 attachments) keeps attached resources alive as long
  as the context lives (`Context::heartbeat` calls `keepAlive()` on each,
  `src/context.cpp:256-258`).
- **Subresources** (`struct Subresource`, `src/context.cpp:67`): the per-context
  installed instance of a resource (e.g. the `ImFont*` for a `Font`). Evicted after
  `SUBRESOURCE_TTL = 120` frames unused (`src/context.hpp:51`, eviction in
  `Context::updateSubresources`, `src/context.cpp:609-622`). **Attachment does NOT
  prevent eviction** — an attached font unused for ~2s gets its `ImFont` removed from
  the atlas and re-rasterized on next use (a hitch).
- GC guard: creating resources while GC ran ≥120 consecutive frames throws
  "excessive creation of short-lived resources".

### Fonts (src/font.cpp, src/font.hpp)
- Dear ImGui **1.92.1** with `IMGUI_HAS_TEXTURES` (`subprojects/imgui`): glyphs bake
  lazily, atlas grows on demand, `ImGuiBackendFlags_RendererHasTextures` is set.
  The old "prebake whole atlas" cost no longer exists upstream.
- ImGui 1.92 **officially supports sharing one `ImFontAtlas` across contexts**, but
  all sharing contexts must render through the same backend/device (texture updates
  are consumed once per atlas). So a shared atlas *requires* a shared GPU device.
- `FontSource` (file-or-memory + face index + style flags) has `operator==`
  (`src/font.cpp:146`) — ready-made dedupe key for a global font registry.
- `SysFont` resolves family names per-platform and installs fallback fonts lazily
  per missing codepoint (`SysFont::addFallback`), caching resolved codepoints.
- Within one script/context, fonts are already shared across all its windows —
  one atlas per context serves every viewport. **The duplication is across scripts
  and across restarts only.**

### Per-call API dispatch (api/helper.hpp, api/callconv.hpp)
Every ReaScript call goes: REAPER Lua bridge (marshals args) → vararg unpacker
`CallConv::ReaScript::apply` (`api/callconv.hpp:36`) → `CallConv::Safe::invoke`
(try/catch + `API::ErrorClearer`) → impl. Impls taking a context start with
`FRAME_GUARD` (`api/helper.hpp:236`) = `assertValid(ctx)` (binary search of global
resource set + `keepAlive()` write) + `enterFrame` (SetCurrentContext + frame check).
Handle args (DrawList proxies, Fonts) each pay `assertValid` too.
- Lua per-call overhead: order of a microsecond (interpreter + bridge dominate).
- Native (C++) callers skip interpreter + marshaling but still pay the safe wrapper +
  FRAME_GUARD per call: tens of ns.
- Bulk-data pattern already exists: `api/plot.cpp` takes `reaper_array*` — one bridge
  crossing for N values. Use this for custom widget data.

### C++ extension surface (for comparison; examples/hello_world.cpp)
- External extensions use generated `reaper_imgui_functions.h` (C API, same
  registered "safe" impls; `ImGui::init(plugin_getapi)` validates version).
- `SetFrameCallback` (`api/context.cpp:325`, `API_FUNC_NATIVE`, native-only, v0.11)
  lets a native extension draw every frame with no defer script.
- External extensions get **no raw `ImGui::*` and no `imgui_internal.h`** — only the
  public ReaImGui API. Real custom widget machinery (`ItemAdd`, `ItemSize`,
  `ButtonBehavior`, custom hit-testing, direct `ImDrawList` `PrimReserve`) is
  **fork-only**.

### Misc per-frame costs
- `Platform::updateMonitors()` runs **every frame, per context** —
  `EnumDisplayMonitors` on Windows (`src/win32_platform.cpp:136`), called from
  `Context::beginFrame` with an upstream `// TODO only if changed`
  (`src/context.cpp:291`). Easy win: cache on `WM_DISPLAYCHANGE`.
- Every open window rebuilds + re-renders + presents every defer tick (~30fps) even
  when idle. No dirty-tracking/power-saving exists.

## 4. The font optimization (the original motivation), concretely

Three layers of "shared fonts", different payoffs:
1. **Between windows of one script** — already free today, no work needed.
2. **Between scripts** — requires: (a) make the D3D10 `Shared` block process-global
   instead of per-`RendererFactory`; (b) one extension-owned `ImFontAtlas` that all
   contexts point their `io.Fonts` at; (c) a global font registry keyed on
   `FontSource` equality so N scripts requesting the same font share one `ImFont`
   (today each context would add a duplicate to a shared atlas).
3. **Across script restarts** — the biggest *felt* win: atlas owned by the extension
   (not any context), house fonts pre-registered at plugin load, exempt from
   `SUBRESOURCE_TTL` and GC. Scripts open with glyphs already baked/uploaded: no
   first-frame rasterization stutter, instant reopen.

Honest sizing: 1.92's lazy baking means steady-state CPU gain is modest. The win is
VRAM (one atlas instead of N × up to ~16MB per 2048² RGBA texture), open/reopen
latency, and hitch elimination. Grows a lot with CJK/icon fonts/many sizes.

**Watch out for lifetime edge cases** (this is where the bugs will live): context
destroyed mid-frame while another context uses the same `ImFont`; atlas
growth/compaction while multiple contexts render; `RemoveFont` refcounting; device
loss with N contexts on one device; teardown order at REAPER exit.

## 5. Full optimization list, ranked

1. **Global shared D3D10 device** — prerequisite for shared atlas + shared image
   textures; also saves per-script device creation (tens of ms, worse on WARP) and
   duplicated shader/state objects. Contained change: the code already hands the
   `Shared` block around via `shared_ptr`.
2. **Persistent shared font atlas + font registry + pinned attached fonts** (see §4).
   Pinning = skip `SUBRESOURCE_TTL` eviction for resources in `m_attachments`
   (trivial patch, kills the 2s-idle re-rasterization hitch independently).
3. **Idle frame-skipping ("power saving")** — skip `ImGui::Render()`/present when no
   input, no animation, no script-driven change for a frame or two; wake on input
   events (Win32 message hooks exist in `src/win32_window.cpp`). Likely the largest
   raw CPU/GPU reduction for a fleet of always-open tool windows. Caveat: ImGui needs
   1–2 frames to settle after any change; scripts still need their defer callbacks to
   run (only skip the render/present, not the script). Upstream unlikely to take
   this — genuine fork material.
4. **Native custom widgets in the fork** (`NvkGui_DrawX(ctx, reaper_array_data, …)`)
   for draw-heavy UIs (grids, meters, arrange-style views). Three performance tiers:
   - Lua per call ≈ µs ⇒ 10–20k calls/frame = ms of pure dispatch;
   - external C++ extension ≈ tens of ns/call but still per-call validation and
     public-API-only;
   - in-fork ≈ zero boundary, `imgui_internal.h` access, precomputed `ImGuiID`s,
     `PrimReserve` bulk vertex writes.
   Since our scripts are Lua, a separate-extension widget must register its own API
   function anyway (1 Lua call either way) — the fork version is strictly better
   *inside* that call. Composite widgets (a few dozen public-API calls) don't
   justify native code; dense draw-heavy ones (10–50k prims, ~1–5ms/frame residual
   tax) do. Drive by profiling, not upfront.
5. **Cheap hygiene**: monitor enumeration on `WM_DISPLAYCHANGE` only (upstreamable);
   share the monitor list across contexts.
6. **House defaults**: default style/theme, bundled brand fonts, docker-merge
   behavior (§2), tuned settings — uniform across all our scripts, zero per-script code.

## 6. Rename / coexistence checklist

### The mechanical rename (small, centralized)
- `API_PREFIX "ImGui_"` → ours, **one define**: `src/api.hpp:24`. Drives all REAPER
  API registrations (`api/helper.hpp:126-129,141`), internal helpers
  (`ImGui__init/__setshim/__shim` in `api/internal.cpp`, `_shim` in
  `src/api.cpp:254`), error strings, and the binding generator outputs for EEL, Lua,
  Python, C++, Zig (`tools/genbinding.cpp:860,1591`).
- DLL name: `meson.build:185` (`reaper_imgui-<arch>` → ours). REAPER loads both DLLs
  side by side. Stock's duplicate-copy check (`src/main.cpp:131`) only looks for its
  own names — won't trip.
- Lua wrapper `shims/imgui.lua`: builds the `ImGui` table **dynamically** from a
  serialized API list returned by native `ImGui__init` — script code (`ImGui.Begin`)
  is prefix-agnostic. Only 3 hardcoded references in the wrapper (lines 2, 6-7, 33)
  plus the file's own name. Scripts using the modern wrapper change only their two
  bootstrap lines (`GetBuiltinPath` + `require`). Legacy `reaper.ImGui_*` calls and
  EEL2 scripts need mechanical find/replace.

### MUST fix — would actively interfere with stock users
1. **Installed files + `GetBuiltinPath`** (worst hazard): `GetBuiltinPath` is
   hardcoded to `Scripts/ReaTeam Extensions/API` (`api/utility.cpp:95`) and the build
   installs `imgui.lua` (`shims/meson.build:13`), `imgui.py` (`meson.build:204`), and
   `gfx2imgui.lua` **into stock's directory**. Fork must use its own directory
   (e.g. `Scripts/NVK/API`), rename the module files (`nvk_imgui.lua` — not a file
   still named `imgui.lua` that could shadow the real one on `package.path`), and
   not ship `gfx2imgui.lua`.
2. **macOS ObjC class collisions**: all compiled `@interface` classes
   (`EventHandler`, `InputView`, `SWELLWindowOverride`, `OpenGLLayer`, …) register
   process-wide by name; duplicates across two loaded plugins = one implementation
   serves both, and associated-object keys are per-dylib static addresses ⇒ silent
   breakage. Also `CocoaInject::makeSubclass` **reuses** an existing runtime class of
   the same generated name (`src/cocoa_inject.mm:57-62`) ⇒ stock and fork would share
   injected subclasses. Prefix every ObjC class in the fork.
3. **Action command IDs**: `plugin_register("command_id", "REAIMGUI_"+name)`
   (`src/action.cpp:89,95`) — identical strings resolve to the same command ID, both
   plugins' hooks respond. Rename ID prefix and visible `"ReaImGui: "` description.
4. **Shared on-disk / registered state**: window-state ini dir
   `<resource>/ReaImGui/*.ini` keyed by context-label hash (`src/context.cpp:88`);
   screenset key prefix `reaimgui:` (`src/context.cpp:753`); shared
   `<resource>/imgui_log.txt` (`src/context.cpp:126`); prefs page ident `"reaimgui"`
   (`src/settings.cpp:465`); doc file `reaper_imgui_doc.html` in shared data dir
   (`meson.build:203`). Rename all.

### Verified SAFE — no action needed
- **Win32 window class**: registered and created with the DLL's own module handle
  (`src/win32_window.cpp:124,163`); Windows keys app-local classes by
  (name, hInstance) ⇒ no cross-DLL conflict. (Rename anyway if paranoid.)
- All REAPER API registrations once `API_PREFIX` changes (nothing else registers
  under a fixed `ImGui` name — audited).
- `timer` / `hookcommand` / `toggleaction` / `prefpage` mechanisms (REAPER chains
  registrants; only ident strings collide, handled above).
- Independent GPU devices/atlases/GCs per extension.

### Belt-and-braces
- **Require REAPER ≥ 7.28**: older versions make ReaImGui subclass REAPER's main
  window proc to detect blocked defer loops (`src/resource.cpp:86`); two plugins
  chain-subclass and teardown deliberately leaks. 7.28+ uses the
  `__reascript_runcnt` config var and never touches the wndproc.
- ReaPack: own repository; package `provides` fully disjoint from stock's.
- Scripts detect the fork via `reaper.APIExists('<Prefix>__init')` with a clear
  install message.
- Post-rename sweep + wire into CI:
  `grep -rn '"ReaImGui\|"reaimgui\|"REAIMGUI\|"imgui\|ReaTeam' src api shims meson.build`
  (remaining hits beyond the list are cosmetic — dialog titles in `src/main.cpp` —
  but this catches upstream-rebase reintroductions).
- Acceptance test, repeated after every rebase: stock running `demo.lua` + fork
  running our scripts simultaneously on all 3 platforms; exercise dock/undock,
  actions list, prefs pages; inspect both plugins' window-state files for cross-talk.
- Cosmetic type names (`"ImGui_Context*"` etc. in `api/types.hpp`,
  `src/context.hpp:146`) don't collide at runtime; rename for clean generated headers.

## 7. Risks & obligations
- **License: LGPLv3.** Forking fine; distributing binaries to users requires making
  modified source available. Internal use unconstrained.
- **Maintenance**: the atlas/device rearchitecture touches the most rebase-conflicting
  code (resource GC, subresource lifetime, renderer). Keep patches small and ordered:
  (1) rebrand, (2) hygiene, (3) device/atlas, (4) widgets.
- **Upstream what upstream will take** (reduces our patch stack): the docker-merge
  patch (§2), the `updateMonitors` TODO. Upstream repo: https://codeberg.org/cfillion/reaimgui
- Fork does NOT need to keep the version-shim discipline — but keep `ImGui__init`'s
  version handshake working for our own wrapper.

## 8. Recommended phasing
1. **Phase 0**: rebrand patch series (§6) + REAPER 7.28 floor + CI grep + coexistence
   acceptance test. Commit the docker patch; open upstream PR for it.
2. **Phase 1**: global D3D10 device → extension-owned shared font atlas with
   `FontSource`-keyed registry → pin attached fonts. Delivers the original goal
   (fonts always loaded, shared, surviving restarts).
3. **Phase 2**: idle frame-skipping.
4. **Phase 3**: native custom widgets, only where profiling shows a script is
   dispatch- or draw-bound; feed data via `reaper_array` (pattern in `api/plot.cpp`).

## 9. Measurements to take (before/after each phase)
- Script open → first rendered frame time (cold and warm).
- VRAM per context (GPU capture will show duplicate atlases today).
- Idle CPU/GPU with our typical window set open (Phase 2 justification).
- Frame time of the heaviest script UI, and its API call count per frame
  (Phase 3 justification) — a simple Lua-side `reaper.time_precise()` wrapper works.

## 10. Build quick-reference (see also CLAUDE.md)
- Meson ≥1.1 + Ninja, C++20. `meson setup build && ninja -C build`;
  tests: `meson test -C build` (GoogleTest). Warnings are errors.
- Bindings regenerate automatically from the API macros via `tools/genbinding.cpp` —
  after the prefix rename, all generated bindings (incl. `reaper_imgui_functions.h`
  and the Python/EEL wrappers) pick up new names with no extra work.
