# Extracts the facial setup and face_rig of the player base heads from the game archives (WolvenKit.Console in
# tools\wolvenkit) and builds the expression editor data: dist\red4ext\plugins\AdvancedPoseMod\face\<head>.json.
# Usage: powershell -ExecutionPolicy Bypass -File tools\extract_face_poses.ps1 [-GamePath <dir>] [-WorkDir <dir>]
#        [-Heads pwa,pma] [-Game 2.5]
# The work folder (archive files and WolvenKit JSON) stays outside the project; tools\face_preview.py reads it.
param(
    [string]$GamePath = "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077",
    [string]$WorkDir = (Join-Path $env:TEMP "apm_face"),
    [string[]]$Heads = @("pwa", "pma"),
    [string]$Game = "2.5"
)
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$wolvenKit = Join-Path $PSScriptRoot "wolvenkit\WolvenKit.CLI.exe"
$outDir = Join-Path $root "dist\red4ext\plugins\AdvancedPoseMod\face"
$folders = @{ pwa = "player_female_average"; pma = "player_man_average" }
if (-not (Test-Path $wolvenKit)) { throw "WolvenKit.Console not found: $wolvenKit" }

$raw = Join-Path $WorkDir "raw"
$json = Join-Path $WorkDir "json"
New-Item -ItemType Directory -Force $raw, $json, $outDir | Out-Null

$names = ($Heads | ForEach-Object { $_ }) -join "|"
$regex = "player_base_heads[\\/]player_(female|man)_average[\\/]h0_000_($names)_c__basehead[\\/].*(_rigsetup\.facialsetup|_skeleton\.rig)$"
Write-Host "Extracting the heads ($($Heads -join ', ')) from $GamePath\archive\pc\content ..."
& $wolvenKit unbundle (Join-Path $GamePath "archive\pc\content") -o $raw -r $regex -v Minimal
if ($LASTEXITCODE -ne 0) { throw "WolvenKit unbundle failed ($LASTEXITCODE)" }
& $wolvenKit convert serialize $raw -o $json -v Minimal
if ($LASTEXITCODE -ne 0) { throw "WolvenKit convert failed ($LASTEXITCODE)" }

foreach ($head in $Heads) {
    if (-not $folders.ContainsKey($head)) { throw "Unknown head '$head' (pwa or pma)" }
    $name = "h0_000_${head}_c__basehead"
    $setup = Join-Path $json "${name}_rigsetup.facialsetup.json"
    $rig = Join-Path $json "${name}_skeleton.rig.json"
    foreach ($file in $setup, $rig) {
        if (-not (Test-Path $file)) { throw "Missing $file (not found in the archives?)" }
    }
    python -I (Join-Path $PSScriptRoot "face_poses.py") $setup $rig (Join-Path $outDir "$head.json") --game $Game
    if ($LASTEXITCODE -ne 0) { throw "face_poses.py failed for $head" }
}
Write-Host "Done: $outDir"
