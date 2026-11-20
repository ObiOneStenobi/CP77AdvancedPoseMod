# Builds dist\red4ext\plugins\AdvancedPoseMod\scene_props.txt (Scene tab list, no characters) from the installed game.
# Extracts every .ent of archive\pc\content and archive\pc\ep1 with WolvenKit (tools\wolvenkit) into -WorkDir,
# serializes them to JSON and runs tools\scene_props_list.py. About 1 GB of temporary files, ~15 minutes.
param(
    [string]$Game = "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077",
    [string]$WorkDir = "$env:TEMP\apm_ents"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$wkit = Join-Path $root "tools\wolvenkit\WolvenKit.CLI.exe"
if (-not (Test-Path $wkit)) { Write-Error "WolvenKit.CLI not found in tools\wolvenkit"; exit 2 }
$raw = Join-Path $WorkDir "raw"
if (Test-Path $raw) { Remove-Item -Recurse -Force $raw }
New-Item -ItemType Directory -Force $raw | Out-Null
$archives = Join-Path $Game "archive\pc"
& $wkit unbundle (Join-Path $archives "content") (Join-Path $archives "ep1") -o $raw -w "*.ent" -v Minimal | Out-Null
# Characters are left out anyway: no need to serialize them.
foreach ($folder in @("base\characters", "ep1\characters")) {
    $path = Join-Path $raw $folder
    if (Test-Path $path) { Remove-Item -Recurse -Force $path }
}
& $wkit convert serialize $raw -v Minimal | Out-Null
$out = Join-Path $root "dist\red4ext\plugins\AdvancedPoseMod\scene_props.txt"
python -I (Join-Path $PSScriptRoot "scene_props_list.py") $raw $out (Join-Path $WorkDir "report.txt")
if ($LASTEXITCODE -ne 0) { exit 1 }
$dist231 = Join-Path $root "dist231\red4ext\plugins\AdvancedPoseMod"
if (Test-Path $dist231) { Copy-Item $out $dist231 -Force }
Write-Host "Report: $(Join-Path $WorkDir 'report.txt')"
