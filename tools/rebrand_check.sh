#!/bin/sh
# Detects reintroduction of stock ReaImGui identifiers after an upstream
# merge. Run from the repository root; exits non-zero on any hit.
#
# src/version.rc.in is excluded because its REAIMGUI_VERSION* macro names
# come from genversion and are not user-visible strings.
hits=$(grep -rn '"ReaImGui\|"reaimgui\|"REAIMGUI\|"imgui\|"ImGui_\|ReaTeam' \
  src api shims meson.build \
  | grep -v 'src/version\.rc\.in' | grep -v 'rebrand-allow')

if [ -n "$hits" ]; then
  echo "stock ReaImGui identifiers found (rebrand regression):" >&2
  echo "$hits" >&2
  exit 1
fi
echo "rebrand check OK"
