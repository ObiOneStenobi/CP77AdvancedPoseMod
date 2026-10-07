# AdvancedPoseMod

Bone-by-bone posing with inverse kinematics for Cyberpunk 2077 Photo Mode.

> Status: **phase 3 – Photo Mode menu**. Body parts of V can be bent/twisted/moved on top of the current
> Photo Mode pose, hands and feet can be pinned with IK, poses can be saved to slots. Everything is available
> at the bottom of the **Pose** page of the Photo Mode menu. A CET debug window is kept for development.

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

## Use

### Pose editor panel

Open Photo Mode and click **Pose editor** (top left; the `<  >` button moves the panel to the other side).

- Tabs pick a body region, the buttons below pick the part.
- Drag a value with the mouse, or use **-** / **+**; **0** resets that value.
- **IK**: choose a hand or foot, **Pin on** keeps it in place, the rows move it and turn the elbow/knee.
- **Poses**: pick a slot (highlighted numbers hold a saved pose), then **Save** / **Load**. **Reset all** asks
  for confirmation.
- **Pose on/off** turns the custom pose on/off; it is turned off when leaving Photo Mode.

### Photo Mode menu items

The same controls are also available at the bottom of the **Pose** page:

1. Open Photo Mode, go to the **Pose** page and scroll to the bottom.
2. **Body part** selects what to edit; **Bend forward / back**, **Bend sideways** and **Twist** rotate it,
   **Move ...** sliders are available for the body (hips).
3. **IK limb** + **IK pin hand / foot** keep a hand/foot in place (e.g. while moving the hips); the IK sliders
   move it and turn the elbow/knee.
4. **Pose slot** + **Save pose to slot** / **Load pose from slot** (slot files: `poses\slot_01.json` ...).
5. **Advanced pose** turns the custom pose on/off; it is turned off when leaving Photo Mode.

The CET overlay still has the **AdvancedPoseMod (debug)** window with raw bone access.

Pose files are written to `<game>\red4ext\plugins\AdvancedPoseMod\poses\<name>.json`.

## Pose file format

```json
{
  "format": "AdvancedPoseMod.Pose",
  "version": 1,
  "name": "my_pose",
  "mode": "additive",
  "skeleton": "V",
  "bones": [
    { "name": "Head", "rotation": [0, 0, 30] },
    { "name": "Hips", "translation": [0, 0, -0.05] }
  ]
}
```

- `rotation`: degrees around the bone's local axes, applied Z * Y * X, on top of the current pose.
- `translation`: meters in the parent bone's space, added to the current pose.
- `ik` (optional): `{ "LeftHand": { "offset": [x, y, z], "swivel": deg }, ... }` for `LeftHand`, `RightHand`,
  `LeftFoot`, `RightFoot`. An enabled chain pins the hand/foot where the original pose put it, plus `offset`
  (meters, character space: X right, Y forward, Z up); `swivel` turns the elbow/knee around the limb axis.

## Layout

```
src/Core      runtime, logging, config, guarded memory reads
src/Game      engine hooks (Photo Mode, animation graph)
src/Math      quaternion helpers
src/Pose      pose targets, override layer, pose files (later: IK)
src/Scripting native functions exposed to redscript / CET (APM_*)
scripts/      redscript: native declarations + Photo Mode menu items
src/Ui        menu state, body part catalog
cet/          developer debug window for Cyber Engine Tweaks
data/         files shipped next to the DLL
docs/         research notes
tools/        developer scripts
```

## Credits

- RED4ext / RED4ext.SDK – WopsS and contributors
- Photo Mode function hashes and structure offsets – psiberx (PhotoModeEx, MIT)
- nlohmann/json – Niels Lohmann (MIT)
