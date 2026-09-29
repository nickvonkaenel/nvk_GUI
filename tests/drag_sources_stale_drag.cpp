// Compiled with the production releaseStaleDrag definition by
// drag_sources_stale_drag.ps1, against a real Dear ImGui context.
//
// A script hands its drag to the OS (re-typing it as an external source with
// the SourceNoPreviewTooltip flag the drag began with), and the files are then
// dropped back on its own window. Context::dragSources must start the files
// drag without inheriting the script's flags, or the tooltip it opens is never
// closed and the frame ends with "Missing End()".
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>

constexpr const char *REAIMGUI_PAYLOAD_TYPE_FILES {"_FILES"};

#include "release_stale_drag.inc"

static std::string g_errors;

static void newFrame(ImGuiIO &io)
{
  io.DeltaTime = 1.f / 30.f;
  ImGui::NewFrame();
}

int main(int argc, char **argv)
{
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  const bool release {argc < 2 || std::strcmp(argv[1], "stale")};

  ImGuiContext *ctx {ImGui::CreateContext()};
  ImGuiIO &io {ImGui::GetIO()};
  io.DisplaySize = ImVec2(800.f, 600.f);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  io.IniFilename = nullptr;
  io.ConfigErrorRecoveryEnableAssert = false;
  io.ConfigErrorRecoveryEnableTooltip = false;
  io.ConfigErrorRecoveryEnableDebugLog = false;
  io.Fonts->AddFontDefault();
  ctx->ErrorCallback = [](ImGuiContext *, void *, const char *msg) {
    g_errors += msg;
    g_errors += '\n';
  };

  // Frame 1: the script's drag, re-typed for the OS drag with its first flags.
  io.AddMousePosEvent(100.f, 100.f);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  newFrame(io);
  if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern |
                                ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
    ImGui::SetDragDropPayload("dnd_file_export", nullptr, 0);
    ImGui::EndDragDropSource();
  }
  ImGui::EndFrame();

  // Frame 2: the files come back over our window with the button still held.
  newFrame(io);
  if(release)
    releaseStaleDrag();
  if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern |
                                ImGuiDragDropFlags_PayloadAutoExpire)) {
    ImGui::SetDragDropPayload(REAIMGUI_PAYLOAD_TYPE_FILES, nullptr, 0);
    ImGui::TextUnformatted("dropped.wav");
    ImGui::EndDragDropSource();
  }
  ImGui::EndFrame();

  const ImGuiPayload *payload {ImGui::IsDragDropActive() ? &ctx->DragDropPayload : nullptr};
  const bool files {payload && payload->IsDataType(REAIMGUI_PAYLOAD_TYPE_FILES)};
  ImGui::DestroyContext(ctx);

  std::printf("mode=%s errors=[%s] files_payload=%d\n",
    release ? "fixed" : "stale", g_errors.c_str(), files);
  std::fflush(stdout);

  if(release)
    return g_errors.empty() && files ? 0 : 1;
  // Negative control: without the release the old drag's flags leak into ours.
  return g_errors.find("Missing End()") != std::string::npos ? 0 : 1;
}
