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

#ifndef REAIMGUI_WINDOW_FLAGS_HPP
#define REAIMGUI_WINDOW_FLAGS_HPP

// Extra ImGuiWindowFlags_ bits that reach the platform layer: unlike the ones
// in api/flags.hpp, which are translated into a window class before Begin, this
// one is passed through so that the docker can read it back off the window.
// Dear ImGui's public flags end at 1 << 19 and its internal ones start at
// 1 << 23; api/flags.hpp takes 1 << 20 for AlwaysOnTop.
enum ReaImGuiWindowFlags_ {
  // Appear without taking keyboard focus, but still make the window visible:
  // a floating window is ordered front (as NoFocusOnAppearing already does on
  // its own), and a docked one has its REAPER docker tab selected instead of
  // being left behind whichever tab is active. Implies NoFocusOnAppearing.
  ReaImGuiWindowFlags_ActivateWithoutFocus = 1 << 21,
};

#endif
