# Cloud build (Linux, Claude sessions)

Builds `AdvancedPoseMod.dll` on Linux with clang-cl and lld-link (cross compile to Windows x64) and checks the
redscript files with the redscript 0.5 compiler. Used by Claude sessions, which run in a Linux container; on Windows
use Visual Studio and `CMakePresets.json` instead.

| Script | What it does |
|---|---|
| `setup.sh` | Once per container: installs clang / lld if missing, downloads the Windows SDK and MSVC CRT (xwin 0.6.5), clones RED4ext.SDK (`ad72777`, plus a `rtti` -> `RTTI` link for Linux), redscript (`v0.5.32`, reads the 2.31 and 2.5 bundles) and Codeware (`613a1cb`, reference only), gets nlohmann/json 3.11.3, builds redscript-cli (Rust lints capped to warnings) and compiles the SDK. About 1.5 GB in `$APM_BUILD` (default `~/apm-build`); a few minutes. |
| `build.sh` | Compiles `src/**/*.cpp` and links `$APM_BUILD/AdvancedPoseMod.dll`. The version comes from `CMakeLists.txt`. |
| `check_scripts.sh <bundle>...` | Compiles `scripts/AdvancedPoseMod/*.reds` against the game's `r6/cache/final.redscripts` (and/or `r6/cache/modded/final.redscripts.modded`), staged from the user's computer. |

Two RED4ext.SDK files (`GpuApi/CommandListContext.cpp`, `GpuApi/DeviceData.cpp`) do not compile with clang; the plugin
does not use them.

Deploy flow used so far: copy the DLL to `dist/red4ext/plugins/AdvancedPoseMod/` and the scripts to
`dist/r6/scripts/AdvancedPoseMod/`; the user copies `dist` into the game.
