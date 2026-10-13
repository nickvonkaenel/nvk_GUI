#include "event_mouse.hpp"

#include <imgui/imgui_internal.h>

void EventMouseInput::position(ImGuiContext *ctx, const ImGuiID viewport,
  const ImVec2 position, const ImVec2 cursor)
{
  if(!m_enabled)
    return;

  m_cursor = cursor;
  m_pending = true;
  ctx->IO.AddMouseViewportEvent(viewport);
  ctx->IO.AddMousePosEvent(position.x, position.y);
}

bool EventMouseInput::shouldPoll(const ImGuiContext *ctx, const ImVec2 cursor)
{
  if(!m_enabled || !m_pending)
    return true;

  if(cursor.x == m_cursor.x && cursor.y == m_cursor.y) {
    for(const ImGuiInputEvent &event : ctx->InputEventsQueue) {
      if(event.Type == ImGuiInputEventType_MousePos ||
         event.Type == ImGuiInputEventType_MouseButton ||
         event.Type == ImGuiInputEventType_MouseViewport)
        return false;
    }
    for(const bool down : ctx->IO.MouseDown) {
      if(down)
        return false;
    }
  }

  reset();
  return true;
}
