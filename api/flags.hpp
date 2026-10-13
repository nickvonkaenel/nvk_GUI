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

#ifndef REAIMGUI_FLAGS_HPP
#define REAIMGUI_FLAGS_HPP

#include "../src/viewport_flags.hpp"
#include "../src/window_flags.hpp"

#include <imgui/imgui_internal.h> // ImHashStr, internal ImGuiInputTextFlags

template<typename T>
class Flags {
public:
  operator T() const       { return m_flags; }
  T *operator *()          { return &m_flags; }
  T operator |=(T op)      { return m_flags |= op; }
  T operator &=(T op)      { return m_flags &= op; }
  T operator &(T op) const { return m_flags & op; }

protected:
  Flags(T flags) : m_flags {flags} {}

private:
  T m_flags;
};

enum {
  // NoBringToFrontOnFocus isn't exposed in ReaImGui
  ReaImGuiWindowFlags_TopMost = ImGuiWindowFlags_NoBringToFrontOnFocus,
  // unused by Dear ImGui: its public flags end at 1 << 19 and the internal
  // ones start at 1 << 23
  ReaImGuiWindowFlags_AlwaysOnTop = 1 << 20,
};

class WindowFlags : public Flags<ImGuiWindowFlags> {
public:
  WindowFlags(int flags) : Flags {flags}
  {
    // Kept in the flags rather than translated: the docker reads it back off
    // the window when its host is created
    if(*this & ReaImGuiWindowFlags_ActivateWithoutFocus)
      *this |= ImGuiWindowFlags_NoFocusOnAppearing;

    if(*this & ReaImGuiWindowFlags_AlwaysOnTop) {
      ImGuiWindowClass alwaysOnTop;
      alwaysOnTop.ClassId = ImHashStr("AlwaysOnTop");
      alwaysOnTop.ViewportFlagsOverrideSet =
        static_cast<ImGuiViewportFlags>(ImGuiViewportFlags_TopMost) |
        static_cast<ImGuiViewportFlags>(ReaImGuiViewportFlags_AlwaysOnTop);
      // the flag is meaningless while docked, don't prevent docking
      alwaysOnTop.DockingAllowUnclassed = true;
      ImGui::SetNextWindowClass(&alwaysOnTop);
      *this &= ~(ReaImGuiWindowFlags_AlwaysOnTop | ReaImGuiWindowFlags_TopMost);
    }
    else if(*this & ReaImGuiWindowFlags_TopMost) {
      ImGuiWindowClass topmost;
      topmost.ClassId = ImHashStr("TopMost");
      topmost.ViewportFlagsOverrideSet = ImGuiViewportFlags_TopMost;
      ImGui::SetNextWindowClass(&topmost);
      *this &= ~ReaImGuiWindowFlags_TopMost; // unset NoBringToFrontOnFocus
    }
    else {
      static ImGuiWindowClass normal;
      ImGui::SetNextWindowClass(&normal);
    }
  }
};

#endif
