# Compiles the mod's scripts with redscript-cli against the game's script bundle, without starting the game.
# Usage: powershell -ExecutionPolicy Bypass -File tools\check_scripts.ps1 [-GamePath <dir>] [-WorkDir <dir>]
# Needs tools\redscript\redscript-cli.exe (release of https://github.com/jac3km4/redscript matching the game's redscript)
# and Codeware installed in the game (its scripts are compiled with the mod's). Only errors in the mod's files count:
# the ones inside Codeware are expected while Codeware is not updated for the game version.
# Exit code: 0 no errors in the mod's files, 1 errors, 2 setup problem. The full log stays in the work folder.
param(
    [string]$GamePath = "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077",
    [string]$WorkDir = (Join-Path $env:TEMP "apm_check"),
    [string]$ScriptsDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "scripts\AdvancedPoseMod")
)
$ErrorActionPreference = "Stop"

$cli = Join-Path $PSScriptRoot "redscript\redscript-cli.exe"
$bundle = Join-Path $GamePath "r6\cache\final.redscripts"
$codeware = Join-Path $GamePath "red4ext\plugins\Codeware\Scripts"
foreach ($file in $cli, $bundle) {
    if (-not (Test-Path $file)) {
        Write-Host "Missing $file" -ForegroundColor Red
        exit 2
    }
}

# Fresh copy of the scripts (the work folder is outside the project)
if (Test-Path $WorkDir) {
    Remove-Item -Recurse -Force $WorkDir
}
$src = Join-Path $WorkDir "src"
New-Item -ItemType Directory -Force (Join-Path $src "AdvancedPoseMod") | Out-Null
Copy-Item (Join-Path $ScriptsDir "*.reds") (Join-Path $src "AdvancedPoseMod")
if (Test-Path $codeware) {
    New-Item -ItemType Directory -Force (Join-Path $src "Codeware") | Out-Null
    Copy-Item (Join-Path $codeware "*") (Join-Path $src "Codeware") -Recurse
} else {
    Write-Host "Codeware not found in the game: its types will be reported as missing" -ForegroundColor Yellow
}

$log = Join-Path $WorkDir "log.txt"
$lines = & $cli compile -s $src -b $bundle -o (Join-Path $WorkDir "out.redscripts") 2>&1 | ForEach-Object { $_.ToString() }
$lines | Out-File $log -Encoding utf8

# A diagnostic starts with "ERROR [...] At <file>:<line>:<col>:" and goes on until the next one. Paths are matched from
# "\src\" on: the compiler may print the long form of a short (8.3) temp path.
$modErrors = 0
$otherErrors = 0
$printing = $false
foreach ($line in $lines) {
    if ($line -match '^(ERROR|WARN) \[') {
        $printing = $false
        if ($line -match '^ERROR \[redscript_compiler') {
            if ($line.Contains("\src\AdvancedPoseMod\")) {
                $modErrors++
                $printing = $true
            } else {
                $otherErrors++
            }
        }
    }
    if ($printing) {
        Write-Host ($line -replace '\S*\\src\\', '')
    }
}

Write-Host ""
Write-Host "Errors in AdvancedPoseMod: $modErrors (other files: $otherErrors). Log: $log"
if ($modErrors -gt 0) {
    exit 1
}
Write-Host "Scripts OK" -ForegroundColor Green
exit 0
