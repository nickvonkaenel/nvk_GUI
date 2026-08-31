-- NvkGui fork coexistence smoke test: runs at REAPER startup, exercises the
-- fork and stock ReaImGui side by side, writes results, then quits REAPER.
-- Usage: /path/to/REAPER tools/coexist_smoke.lua  (with both the stock
-- ReaImGui and the NvkGui fork installed). Writes the result next to
-- this script and quits REAPER when done.
local RESULT = debug.getinfo(1, 'S').source:match('^@(.*)[/\\]')
  .. '/coexist_smoke_result.txt'

local results = {}
local function check(name, ok, extra)
  results[#results+1] = ('%s: %s%s'):format(ok and 'PASS' or 'FAIL', name,
    extra and (' [' .. tostring(extra) .. ']') or '')
end

local function finish()
  local f = io.open(RESULT, 'w')
  f:write(table.concat(results, '\n'), '\n')
  f:close()
  reaper.Main_OnCommand(40004, 0) -- File: Quit REAPER
end

check('fork API registered (NvkGui__init)', reaper.APIExists('NvkGui__init'))
check('fork API registered (NvkGui_GetVersion)', reaper.APIExists('NvkGui_GetVersion'))
check('stock API registered (ImGui_GetVersion)', reaper.APIExists('ImGui_GetVersion'))

if not reaper.APIExists('NvkGui__init') or not reaper.APIExists('ImGui_GetVersion') then
  check('aborting: missing one of the two plugins', false)
  return finish()
end

check('distinct builtin paths', reaper.NvkGui_GetBuiltinPath() ~= reaper.ImGui_GetBuiltinPath(),
  reaper.NvkGui_GetBuiltinPath() .. ' vs ' .. reaper.ImGui_GetBuiltinPath())

package.path = reaper.NvkGui_GetBuiltinPath() .. '/?.lua;'
  .. reaper.ImGui_GetBuiltinPath() .. '/?.lua;' .. package.path

local okN, NvkGui = pcall(function() return require 'nvkgui' '0.10' end)
check('require nvkgui wrapper', okN, okN and NvkGui.GetVersion() or NvkGui)
local okS, ImGui = pcall(function() return require 'imgui' '0.9' end)
check('require stock imgui wrapper', okS, okS and ImGui.GetVersion() or ImGui)
if not (okN and okS) then return finish() end

check('fork has FontFlags_SystemFallback', NvkGui.FontFlags_SystemFallback ~= nil,
  tostring(NvkGui.FontFlags_SystemFallback))

local fctx = NvkGui.CreateContext('NvkGui smoke')
local sctx = ImGui.CreateContext('ReaImGui smoke')
check('fork CreateContext', fctx ~= nil)
check('stock CreateContext', sctx ~= nil)

-- Cross-handle rejection verified manually: passing sctx to NvkGui_Begin
-- aborts with "expected a valid NvkGui_Context*" (and vice versa). Not
-- testable inline because ReaScriptError("!...") kills the whole script.

local frames = 0
local function loop()
  local ok1, err1 = pcall(function()
    if NvkGui.Begin(fctx, 'NvkGui window') then
      NvkGui.Text(fctx, 'hello from the fork, frame ' .. frames)
      NvkGui.End(fctx)
    end
  end)
  local ok2, err2 = pcall(function()
    if ImGui.Begin(sctx, 'Stock window') then
      ImGui.Text(sctx, 'hello from stock, frame ' .. frames)
      ImGui.End(sctx)
    end
  end)
  if not ok1 or not ok2 then
    check('frame ' .. frames .. ' fork', ok1, err1)
    check('frame ' .. frames .. ' stock', ok2, err2)
    return finish()
  end
  frames = frames + 1
  if frames < 90 then
    reaper.defer(loop)
  else
    check('90 frames rendered in both plugins simultaneously', true)
    finish()
  end
end
reaper.defer(loop)
