# nvk_GUI: a ReaImGui fork for nvk scripts

A renamed fork of [ReaImGui](https://codeberg.org/cfillion/reaimgui) that
**coexists** with the stock extension: it registers its API as
`NvkGui_*`, ships as `reaper_nvkgui`, installs its Lua wrapper as
`Scripts/nvk/API/nvkgui.lua` (a development build) or
`Scripts/nvk-ReaScripts/Extensions/nvkgui.lua` (the nvk_API ReaPack package),
and keeps all of its on-disk and registered
state (ini files, screensets, action IDs, prefs page, ObjC classes) under
its own names. Stock ReaImGui keeps serving third-party scripts; our
scripts depend on this fork. Requires REAPER 7.28+.

This repository is developed in place as the `vendor/nvk_GUI` submodule of
nvk_API (`~/repos/nvk_API/vendor/nvk_GUI`), and that is deliberately the only
working copy: the standalone clone that used to sit beside it drifted four
commits behind and swallowed a change patched into it. Commit here, push, then
bump the submodule pointer in nvk_API. The Dear ImGui patches this fork needs
live on the `nvk-patches` branch of `https://github.com/nickvonkaenel/imgui`;
keep them on that branch and pushed, or `git submodule update` will strand
them.

Branch layout:
- `main` — the fork: upstream + our patch series (rebrand first, then
  functional changes).
- `archive/*` — retired history kept for reference: `pre-nvk_GUI-rewrite`
  (the lineage before the rebrand rewrite) and `font-system-fallback-pr`
  (the branch prepared for an upstream PR).
- `master` — tracks `upstream/master` (cfillion's Codeberg). To pull
  upstream changes: `git fetch upstream && git checkout master &&
  git merge --ff-only upstream/master && git checkout main &&
  git merge master`, then run `tools/rebrand_check.sh` and the coexistence
  smoke test (`tools/coexist_smoke.lua`, stock and fork loaded in one REAPER).

Upstream ReaImGui lives at <https://codeberg.org/cfillion/reaimgui>: issues,
releases and the ReaPack distribution of the stock extension belong there, not
here. This fork is distributed only inside the nvk_API ReaPack package, which
installs it beside nvk_API; development builds are installed by nvk_API's
`build.sh`.

Licensed under the LGPL v3 or later, like upstream (see COPYING.LESSER).

## macOS developer input testing

Targeted automated mouse clicks are opt-in. With REAPER closed, set the following
in its resource-directory `REAPER.ini`, inside the existing `[nvkgui]` section:

```ini
[nvkgui]
dev_automation_input=1
```

Restart REAPER after changing it. Only the value `1` enables event-position input;
an absent key or any other value leaves the original system-cursor input path in
place. This hidden setting is local to that installation, is never enabled or
written by the extension, and is not part of script defaults or the preferences UI.
Remove the key or set it to `0` with REAPER closed to disable it.

When enabled, button events supply their own position until ImGui processes the
gesture. Physical cursor movement immediately resumes normal polling. The flag
does not enable automated dragging or scrolling, and does not affect stock ReaImGui.
`tests/event_mouse_test.cpp` covers both modes, including normal clicks, movement
during a drag, release cancellation, focus reset, and the default-off path.

## Build from source

On macOS this is normally built for you: nvk_API's `./build.sh` runs the Meson
steps below with `-Dresource_path` pointed at the REAPER install, after
building nvk_API itself. The rest of this section is for building the extension
on its own.

### Prerequisites

Software requirements:

- [Meson](https://mesonbuild.com/) (1.1 or newer)
- C++20 compiler* (C++17 is supported for Linux and macOS builds)
- PHP (optional, for preprocessing of gfx2imgui.lua)

(* The C++ compiler must be ABI-compatible with MSVC on Windows)

Library requirements:

By default these will be picked up from the system or downloaded if missing.

- [Boost](https://www.boost.org/) (1.81 or newer)
- [fmt](https://fmt.dev/) (only if C++20 is not available)
- [FreeType](https://www.freetype.org/)
- [GoogleTest](https://github.com/google/googletest)
- [libjpeg-turbo](https://www.libjpeg-turbo.org/)
- [libpng](http://www.libpng.org/pub/png/libpng.html)
- [md4c](https://github.com/mity/md4c)
- [zlib](https://www.zlib.net/)

Additional system dependencies on Linux:

- [Fontconfig](https://www.fontconfig.org)
- [GDK3](https://developer.gnome.org/gdk3/stable/) (3.22 or newer)
- [libepoxy](https://github.com/anholt/libepoxy)

### Compile and install

In the nvk_API checkout the sources are already there; make sure this fork's own
submodules are:

    $ git submodule update --init --recursive

On a machine that has no nvk_API checkout, clone it with its submodules (never
as a second copy beside that one):

    $ git clone --recursive --shallow-submodules https://github.com/nickvonkaenel/nvk_GUI.git

Build nvk_GUI using Meson:

    $ meson setup build
    $ cd build
    $ ninja

To change the installation directory to a portable install:

    $ meson configure -Dresource_path=~/path/to/your/portable/install/

Downgrade to C++17 if compiling on macOS with Xcode older than 14:

    $ meson configure -Dcpp_std=c++17

Run the test suite using:

    $ meson test

Install nvk_GUI into your REAPER resource path using:

    $ meson install --tags runtime

### Cross-compilation

#### Linux

QEMU is used to run compiled binaries for binding generation and tests.

    meson setup build/i686    --cross-file cross/i686-linux-gnu.ini
    meson setup build/armv7l  --cross-file cross/arm-linux-gnueabihf.ini
    meson setup build/aarch64 --cross-file cross/aarch64-linux-gnu.ini

#### macOS

macOS 10.14 (or older) and Xcode 9 are required for producing 32-bit builds.
Xcode 12 is required to compile for ARM64.

    export MACOSX_DEPLOYMENT_TARGET=10.9
    meson setup build/i386 --cross-file cross/i386-darwin.ini

    export MACOSX_DEPLOYMENT_TARGET=11.0
    meson setup build/arm64 --cross-file cross/arm64-darwin.ini
