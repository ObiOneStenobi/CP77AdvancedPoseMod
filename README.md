# AdvancedPoseMod

Bone-by-bone posing with inverse kinematics for Cyberpunk 2077 Photo Mode.

> Status: **phase 0 – research probe / proof of concept**. The plugin hooks the animation graph output,
> finds V's Photo Mode puppet and rotates a single bone taken from `config.ini`.

## Requirements

- Cyberpunk 2077 2.31
- [RED4ext](https://docs.red4ext.com) 1.30+
- Visual Studio 2022 with the **Desktop development with C++** workload (includes CMake and Git)

## Build

1. Open Visual Studio 2022 → **Open a local folder** → select this folder.
2. Visual Studio detects `CMakePresets.json`. Choose the **Visual Studio 2022 (x64)** configure preset
   and the **Release** build preset.
3. **Build → Build All**. The first configure downloads RED4ext.SDK (needs internet).

Command line alternative (Developer PowerShell for VS 2022):

```powershell
cmake --preset vs2022
cmake --build --preset release
```

## Deploy for testing

```powershell
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1
```

This copies `AdvancedPoseMod.dll` (and `config.ini` the first time) into
`<game>\red4ext\plugins\AdvancedPoseMod\`.

## Test (phase 0)

1. Start the game, load a save, open Photo Mode.
2. With the default config V's head should be rotated by 45° around its local Z axis.
3. Close the game and collect:
   - `<game>\red4ext\logs\advancedposemod-*.log` (newest)
   - `<game>\red4ext\plugins\AdvancedPoseMod\probe\rig_*.txt`

## Layout

```
src/Core      runtime, logging, config, guarded memory reads
src/Game      engine hooks (Photo Mode, animation graph)
src/Math      quaternion helpers
src/Pose      pose targets and (later) override layer + IK
data/         files shipped next to the DLL
docs/         research notes
tools/        developer scripts
```

## Credits

- RED4ext / RED4ext.SDK – WopsS and contributors
- Photo Mode function hashes and structure offsets – psiberx (PhotoModeEx, MIT)
