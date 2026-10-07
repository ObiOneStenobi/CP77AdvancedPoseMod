<#
.SYNOPSIS
    Copies the built plugin into the game folder for testing.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\deploy.ps1
    powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Configuration Debug
#>
param(
    [string]$GamePath = "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077",
    [ValidateSet("RelWithDebInfo", "Debug", "Release")]
    [string]$Configuration = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build\$Configuration"
$target = Join-Path $GamePath "red4ext\plugins\AdvancedPoseMod"

if (-not (Test-Path (Join-Path $GamePath "bin\x64\Cyberpunk2077.exe"))) {
    throw "Cyberpunk 2077 not found at '$GamePath'. Use -GamePath to set it."
}

$dll = Join-Path $buildDir "AdvancedPoseMod.dll"
if (-not (Test-Path $dll)) {
    throw "Build output not found: $dll. Build the project first."
}

New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item $dll $target -Force
$pdb = Join-Path $buildDir "AdvancedPoseMod.pdb"
if (Test-Path $pdb) { Copy-Item $pdb $target -Force }

# Never overwrite a config the user already edited.
$config = Join-Path $target "config.ini"
if (-not (Test-Path $config)) {
    Copy-Item (Join-Path $root "data\config.ini") $config
}

Write-Host "Deployed AdvancedPoseMod ($Configuration) to $target"
