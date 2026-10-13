#ifndef REAIMGUI_EVENT_MOUSE_HPP
#define REAIMGUI_EVENT_MOUSE_HPP

#include <imgui/imgui.h>

// Event positions may differ from the system cursor (targeted automation).
// Keep them through the queued button gesture, but yield to physical movement.
class EventMouseInput {
public:
  explicit EventMouseInput(bool enabled = false) : m_enabled {enabled} {}
  bool enabled() const { return m_enabled; }
  void position(ImGuiContext *, ImGuiID viewport, ImVec2 position, ImVec2 cursor);
  bool shouldPoll(const ImGuiContext *, ImVec2 cursor);
  void reset() { m_pending = false; }

private:
  bool m_enabled;
  ImVec2 m_cursor {};
  bool m_pending {};
};

#endif
