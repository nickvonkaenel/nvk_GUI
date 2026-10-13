#include "../src/event_mouse.hpp"

#include <imgui/imgui_internal.h>
#include <gtest/gtest.h>

class EventMouseTest : public testing::Test {
protected:
  void SetUp() override
  {
    ctx = ImGui::CreateContext();
    auto &io {ctx->IO};
    io.DisplaySize = ImVec2(640, 480);
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ASSERT_NE(io.Fonts->AddFontFromFileTTF(TEST_FONT_PATH, 13.f), nullptr);
    frame();
    frame();
  }

  void TearDown() override { ImGui::DestroyContext(ctx); }

  bool frame()
  {
    if(input.shouldPoll(ctx, cursor))
      ctx->IO.AddMousePosEvent(cursor.x, cursor.y);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowSize(ImVec2(320, 200));
    ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoSavedSettings);
    const bool clicked {ImGui::Button("button", ImVec2(180, 70))};
    const ImVec2 min {ImGui::GetItemRectMin()}, max {ImGui::GetItemRectMax()};
    button = ImVec2((min.x + max.x) / 2, (min.y + max.y) / 2);
    hovered = ImGui::IsItemHovered();
    ImGui::End();
    ImGui::EndFrame();
    return clicked;
  }

  void event(bool down, ImVec2 point)
  {
    input.position(ctx, ImGui::GetMainViewport()->ID, point, cursor);
    ctx->IO.AddMouseButtonEvent(0, down);
  }

  ImGuiContext *ctx {};
  EventMouseInput input {true};
  ImVec2 cursor {600, 400}, button {};
  bool hovered {};
};

TEST_F(EventMouseTest, TargetedClickBetweenFrames)
{
  event(true, button);
  event(false, button);
  EXPECT_FALSE(frame());
  EXPECT_TRUE(hovered);
  EXPECT_TRUE(ctx->IO.MouseDown[0]);
  EXPECT_TRUE(frame());
  EXPECT_FALSE(ctx->IO.MouseDown[0]);
  EXPECT_FALSE(frame());
  EXPECT_FALSE(hovered);
  EXPECT_EQ(ctx->IO.MousePos.x, cursor.x);
}

TEST_F(EventMouseTest, HeldTargetedClickKeepsPositionUntilRelease)
{
  event(true, button);
  frame();
  for(int i {}; i < 4; ++i) {
    EXPECT_FALSE(frame());
    EXPECT_TRUE(hovered);
  }
  event(false, button);
  EXPECT_TRUE(frame());
  frame();
  EXPECT_FALSE(hovered);
}

class NormalMouseTest : public EventMouseTest,
  public testing::WithParamInterface<bool> {
protected:
  void SetUp() override
  {
    input = EventMouseInput {GetParam()};
    EventMouseTest::SetUp();
  }
};

INSTANTIATE_TEST_SUITE_P(AutomationInput, NormalMouseTest, testing::Bool());

TEST_P(NormalMouseTest, PhysicalClickStillActivates)
{
  cursor = button;
  frame();
  event(true, cursor);
  EXPECT_FALSE(frame());
  event(false, cursor);
  EXPECT_TRUE(frame());
  EXPECT_FALSE(frame());
  EXPECT_TRUE(hovered);
}

TEST_P(NormalMouseTest, RealMovementDuringDragResumesPolling)
{
  cursor = button;
  event(true, cursor);
  frame();
  cursor = ImVec2(500, 400);
  frame();
  EXPECT_EQ(ctx->IO.MousePos.x, 500);
  EXPECT_EQ(ctx->IO.MousePos.y, 400);
  EXPECT_FALSE(hovered);
  event(false, cursor);
  EXPECT_FALSE(frame()); // Released outside the button: no activation.
}

TEST_F(EventMouseTest, CapturedReleaseOutsideDoesNotClick)
{
  event(true, button);
  frame();
  event(false, ImVec2(500, 400));
  EXPECT_FALSE(frame());
  EXPECT_FALSE(hovered);
}

TEST_F(EventMouseTest, ConsecutiveClicksRemainOrdered)
{
  event(true, button);
  event(false, button);
  event(true, button);
  event(false, button);
  int clicks {};
  for(int i {}; i < 8; ++i)
    clicks += frame();
  EXPECT_EQ(clicks, 2);
  EXPECT_FALSE(hovered);
}

TEST_P(NormalMouseTest, FocusResetAllowsPhysicalPolling)
{
  event(true, button);
  frame();
  ctx->IO.ClearInputKeys();
  ctx->IO.ClearInputMouse();
  input.reset();
  frame();
  EXPECT_FALSE(ctx->IO.MouseDown[0]);
  EXPECT_FALSE(hovered);
  EXPECT_EQ(ctx->IO.MousePos.x, cursor.x);
}

TEST_F(EventMouseTest, DefaultOffIgnoresEventCoordinates)
{
  input = EventMouseInput {};
  EXPECT_FALSE(input.enabled());
  const int queued {ctx->InputEventsQueue.Size};
  input.position(ctx, ImGui::GetMainViewport()->ID, button, cursor);
  EXPECT_EQ(ctx->InputEventsQueue.Size, queued);
  EXPECT_TRUE(input.shouldPoll(ctx, cursor));

  event(true, button);
  EXPECT_FALSE(frame());
  EXPECT_FALSE(hovered);
  event(false, button);
  EXPECT_FALSE(frame());
  EXPECT_EQ(ctx->IO.MousePos.x, cursor.x);
  EXPECT_EQ(ctx->IO.MousePos.y, cursor.y);
}
