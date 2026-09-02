# nvk_GUI: a ReaImGui fork for nvk scripts

A renamed fork of [ReaImGui](https://codeberg.org/cfillion/reaimgui) that
**coexists** with the stock extension: it registers its API as
`NvkGui_*`, ships as `reaper_nvkgui`, installs its Lua wrapper as
`Scripts/nvk/API/nvkgui.lua`, and keeps all of its on-disk and registered
state (ini files, screensets, action IDs, prefs page, ObjC classes) under
its own names. Stock ReaImGui keeps serving third-party scripts; our
scripts depend on this fork. Requires REAPER 7.28+.

This repository is developed in place as the `vendor/nvk_GUI` submodule of
nvk_API (`~/repos/nvk_API/vendor/nvk_GUI`); that is the only working copy.
A second clone drifts behind and swallows changes patched into it, so
`~/repos/reaimgui` is a symlink here. Commit here, push, then bump the
submodule pointer in nvk_API. The Dear ImGui patches this fork needs live on
the `nvk-patches` branch of `https://github.com/nickvonkaenel/imgui`; keep them
on that branch and pushed, or `git submodule update` will strand them.

Branch layout:
- `main` — the fork: upstream + our patch series (rebrand first, then
  functional changes).
- `archive/*` — retired history kept for reference: `pre-nvk_GUI-rewrite`
  (the lineage before the rebrand rewrite) and `font-system-fallback-pr`
  (the branch prepared for an upstream PR).
- `master` — tracks `upstream/master` (cfillion's Codeberg). To pull
  upstream changes: `git fetch upstream && git checkout master &&
  git merge --ff-only upstream/master && git checkout main &&
  git merge master`, then run `tools/rebrand_check.sh` and the
  coexistence smoke test (stock + fork loaded in one REAPER instance).

Licensed under the LGPL v3 or later, like upstream (see COPYING.LESSER).

---

Original upstream README follows.

# ReaImGui: ReaScript binding for Dear ImGui

[![Build status](https://ci.appveyor.com/api/projects/status/9umkecgrs4sa8odf/branch/master?svg=true)](https://ci.appveyor.com/project/cfillion/reaimgui/branch/master)

This REAPER extension is distributed via [ReaPack](https://reapack.com)
through the default [ReaTeam Extensions](https://github.com/ReaTeam/Extensions)
repository.

https://forum.cockos.com/showthread.php?t=250419

## Build from source

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

Fetch the repository including submodules:

    $ git clone --recursive --shallow-submodules https://codeberg.org/cfillion/reaimgui.git

Build ReaImGui using Meson:

    $ meson setup build
    $ cd build
    $ ninja

The change the installation directory to a portable install:

    $ meson configure -Dresource_path=~/path/to/your/portable/install/

Downgrade to C++17 if compiling on macOS with Xcode older than 14:

    $ meson configure -Dcpp_std=c++17

Run the test suite using:

    $ meson test

Install ReaImGui into your REAPER resource path using:

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
