# AdvancedPoseMod

Bone-by-bone posing with inverse kinematics for Cyberpunk 2077 Photo Mode.

> Status: **phase 4 – pose editor**. V can be posed bone by bone in Photo Mode from the **Pose editor** panel and
> with gizmos on the joints (rotation, IK for hands and feet, eyes), and whole poses can be saved and loaded.

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

Open Photo Mode and click **Pose editor** (top left).

- Header: **Pose on/off** (custom pose switch), **Gizmos** (joint markers on/off), panel **opacity** (100/75/50/25 %),
  `<  >` moves the panel to the other side, **Hide**. These settings are remembered in `panel.ini`.
- Tabs pick a body region, the buttons below pick the part.
- Drag a value with the mouse, or use **-** / **+**; **0** resets that value.
- **IK**: choose a hand or foot, **Pin** keeps it in place (world or body), the rows move it and turn the elbow/knee.
- **Poses**: unlimited library (`poses\*.json`), five per page. **Save new** saves the pose and lets you type its
  name right away (Enter confirms, Esc keeps the default "Pose 001", ...). Select a pose to **Overwrite**, **Load**,
  **Rename** or **Delete** it (delete asks for a second click). While a name is being typed, the keyboard only
  writes the name (Photo Mode keys are ignored); names use letters, digits, spaces, `-` and `_`.
- A saved pose stores the **whole visible pose** (every body bone, edits and IK included), not just the edits:
  loading it replaces the Photo Mode pose underneath, whatever pose is selected there, and new edits go on top of
  it. The face keeps its Photo Mode expression. **Reset all** goes back to the Photo Mode pose. Poses saved by
  versions before 0.8.1 contain only edits and are still applied on top of the current Photo Mode pose.

### Gizmos

Markers sit on V's joints, linked by lines like a skeleton. Their shape shows what a drag does with the keys held
right now (diamond = rotate, square = move, flat bar = twist; orange = IK), and a label next to the hovered marker says
it too. Finger markers appear when
the camera is close to a hand (or while a hand tab is selected); markers for the outer finger segments appear even
closer.

- Click a marker to select that body part in the panel.
- Drag a white marker to rotate the part: the end of the bone follows the cursor. The head turns as seen from the
  camera instead (left/right around the screen's vertical axis, up/down around its horizontal axis).
- **Ctrl+drag** any marker to twist the part around the bone itself.
- Drag an orange marker (hands and feet) and it follows the cursor on a plane facing the camera (IK, pinned
  automatically). **Shift+drag** it to rotate the hand / foot instead.
- The **Pin** button cycles **off / world / body**: *world* keeps the hand / foot where it is whatever the body does;
  *body* makes it follow the shoulder (hips) it hangs from, keeping the arm's (leg's) pose while the torso turns.
  Switching between world and body leaves the hand / foot where it is.
- While a limb is pinned, dragging its upper arm / forearm (thigh / shin) marker turns the elbow (knee) around the
  shoulder -> hand line.
- The orange marker floating in front of the face is where the eyes look: drag it to move their gaze (it
moves on a sphere around the eyes).
- Shift / Ctrl can be pressed or released during a drag: the action changes on the fly, keeping what was done.
- Close-up gizmos (camera near the hand / foot): palm bones (cup or spread the hand), thumb base, the point a held
  prop hangs from (rotate it, or move it with the panel's Move rows) and the heel.
- Moving a whole finger keeps the extra bend of its segments.

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
scripts/      redscript: native declarations + pose editor panel and gizmos
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
