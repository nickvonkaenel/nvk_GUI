#ifndef REAIMGUI_EVENT_MOUSE_HPP
#define REAIMGUI_EVENT_MOUSE_HPP

#include <imgui/imgui.h>

// Event positions may differ from the system cursor (targeted automation).
// Keep them through the queued button gesture, but yield to physical movement.
class EventMouseInput {
public:
  void position(ImGuiContext *, ImGuiID viewport, ImVec2 position, ImVec2 cursor);
  bool shouldPoll(const ImGuiContext *, ImVec2 cursor);
  void reset() { m_pending = false; }

private:
  ImVec2 m_cursor {};
  bool m_pending {};
};

#endif
