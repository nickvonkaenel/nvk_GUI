param([string]$SourcePath, [string]$ImGuiPath)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $SourcePath) { $SourcePath = Join-Path $repo 'src/context.cpp' }
if (-not $ImGuiPath) { $ImGuiPath = Join-Path $repo 'subprojects/imgui/imgui' }
$source = Get-Content -LiteralPath $SourcePath -Raw
$definition = [regex]::Match($source, '(?ms)^static void releaseStaleDrag\(\)\r?\n\{.*?^\}')
if (-not $definition.Success) { throw 'releaseStaleDrag definition not found' }
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('nvkgui-drag-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $scratch | Out-Null
Set-Content -LiteralPath (Join-Path $scratch 'release_stale_drag.inc') -Value $definition.Value
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC installation not found' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$testSource = Join-Path $PSScriptRoot 'drag_sources_stale_drag.cpp'
$build = @"
@call "$vcvars" >nul
@cl /nologo /EHsc /std:c++20 /DUNICODE /I"$ImGuiPath" /I"$scratch" "$testSource" "$ImGuiPath/imgui.cpp" "$ImGuiPath/imgui_draw.cpp" "$ImGuiPath/imgui_tables.cpp" "$ImGuiPath/imgui_widgets.cpp" /Fe:drag.exe /link user32.lib gdi32.lib imm32.lib
"@
Set-Content -LiteralPath (Join-Path $scratch 'build.cmd') -Value $build
Push-Location $scratch
try {
  & $env:ComSpec /d /c build.cmd
  if ($LASTEXITCODE) { throw 'Regression test compilation failed' }
  $failures = @()
  # 'fixed' runs the production release; 'stale' proves the test sees the bug without it.
  foreach ($mode in @('fixed', 'stale')) {
    & ./drag.exe $mode
    if ($LASTEXITCODE) { $failures += "stale drag regression ($mode): exit $LASTEXITCODE" }
  }
  if ($failures.Count) { throw ($failures -join '; ') }
  Write-Host 'PASS: files dropped back on the window start a clean drag; the stale-flag failure is still detected'
} finally { Pop-Location }
