// Compiled with the production effectiveFlags definition by
// window_effective_flags.ps1, using real Dear ImGui contexts and viewports.
#include <imgui.h>
#include <imgui_internal.h>
#include "viewport_flags.hpp"
#include <cstdio>
#include <cstring>
#include <windows.h>

struct Context {
  ImGuiContext *value;
  ImGuiContext *imgui() const { return value; }
};

struct Window {
  Context *m_ctx;
  ImGuiViewport *m_viewport;
  ImGuiViewportFlags effectiveFlags() const;
};

#include "effective_flags.inc"

int main(int argc, char **argv)
{
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  ImGuiContext *owner {ImGui::CreateContext()};
  ImGuiContext *other {ImGui::CreateContext()};
  Context context {owner};
  ImGuiViewportP child;
  child.ID = 42;
  child.Flags = ImGuiViewportFlags_NoDecoration;
  child.ParentViewportId = owner->Viewports[0]->ID;
  owner->Viewports[0]->Flags |= ReaImGuiViewportFlags_AlwaysOnTop;
  Window window {&context, &child};
  ImGuiContext *current {argc > 1 && !std::strcmp(argv[1], "null") ? nullptr : other};
  ImGui::SetCurrentContext(current);
  std::puts("Checking owning-context inheritance outside a frame");
  std::fflush(stdout);
  auto check = [&](bool condition, const char *message) {
    if(!condition) { std::fprintf(stderr, "FAIL: %s\n", message); return false; }
    return true;
  };
  bool passed = check(window.effectiveFlags() ==
    (child.Flags | ReaImGuiViewportFlags_AlwaysOnTop), "owner always-on-top was not inherited");
  passed &= check(ImGui::GetCurrentContext() == current, "current context changed");
  child.ParentViewportId = 123456;
  passed &= check(window.effectiveFlags() == child.Flags, "missing parent changed flags");
  child.ParentViewportId = 0;
  passed &= check(window.effectiveFlags() == child.Flags, "parentless window changed flags");
  // Two-level ancestry and a cycle without an always-on-top ancestor.
  auto *parent = IM_NEW(ImGuiViewportP)();
  parent->ID = 43;
  parent->ParentViewportId = owner->Viewports[0]->ID;
  owner->Viewports.push_back(parent);
  child.ParentViewportId = parent->ID;
  passed &= check(window.effectiveFlags() & ReaImGuiViewportFlags_AlwaysOnTop,
    "grandparent always-on-top was not inherited");
  parent->ParentViewportId = parent->ID;
  passed &= check(window.effectiveFlags() == child.Flags, "cycle changed flags");
  ImGui::DestroyContext(other);
  ImGui::DestroyContext(owner);
  return passed ? 0 : 1;
}
