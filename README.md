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
**X** hides the panel. These settings are remembered in `panel.ini`.
  Below them icon buttons (rest the mouse on any button for a moment to see a tooltip above it): **Undo**, **Redo**, **Reset part** (eraser; a pinned hand / foot also goes back to
  where the pose puts it), **Reset all** (click
  twice), **Mirror**, and **?** which opens the info window (marker legend and shortcuts). Choosing an IK limb also selects that hand / foot above. The IK limb buttons get an orange frame,
  and the hand / foot marker an orange glow, while that limb is pinned. Messages after an action (saved, loaded,
  click again...) appear for a moment above the button that was clicked.
- The panel only shows what applies: the Move rows for parts that can move (hips, held items), the IK rows while the
  chosen limb is pinned. The pose library folds with the arrow on its right (closed when the game starts, then remembered until it closes).
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
- **Alt+drag** any marker to rotate the part freely, like a ball under the cursor: left / right turns it around the
  camera's vertical axis, up / down around its horizontal axis (as the head always does).
- Drag an orange marker (hands and feet) and it follows the cursor on a plane facing the camera (IK, pinned
  automatically). **Shift+drag** it to rotate the hand / foot instead.
- The pin row (**Off / World / Body**, under the limb buttons) sets the pin: *world* keeps the hand / foot where it is whatever the body does;
  *body* makes it follow the shoulder (hips) it hangs from, keeping the arm's (leg's) pose while the torso turns.
  Turning the pin on, or switching between world and body, leaves the hand / foot where it is.
- While a limb is pinned, dragging its upper arm / forearm (thigh / shin) marker turns the elbow (knee) around the
  shoulder -> hand line.
- The orange marker in front of the face is the point both eyes look at: drag it and the eyes converge on it.
- Shift / Ctrl / Alt can be pressed or released during a drag: the action changes on the fly, keeping what was done.
- Close-up gizmos (camera near the hand): palm bones (cup or spread the hand) and thumb base.
- **Hand prop** (green marker, also in the hand tab): the point a held weapon / prop hangs from. Drag to rotate it,
  **Shift+drag** to move it. Dragging the hips moves the whole body (**Shift+drag** rotates them); while the hips are selected a grey
  diamond shows V's root (where Photo Mode placed her), with a line to the hips: saved poses keep the hips
  position relative to that point. The eyes turn at most 30 degrees from where the pose points them; with **Eyes (both)** the
  third row is their **focus distance** (nearer = more converged).
- **Auto IK** (magnet icon, on by default): dragging a hand / foot moves it with IK and pins it, Shift+drag rotates
  it. Turned off, dragging rotates the hand / foot like any other bone; Shift+drag moves it with IK only if that
  limb is already pinned in the IK row. Turning a pin off keeps the arm / leg where the IK put it (Reset part undoes it).
  With joint limits on, a pinned limb also stops where the hip / shoulder and knee / elbow can no longer follow.
- **Eyes**: the Head tab has one **Eyes** part; its **Both / Left / Right** switch picks one look marker for both eyes
  or one marker per eye.
- The hips marker is violet: it is V's root and moves the whole body.
- **Hand presets** (in the Left hand / Right hand tabs, and under the hand part of an arm tab): Open, Relaxed, Fist, Point, Grip set all the fingers of that
  hand at once (undo and Mirror work as usual); then refine with the finger markers.
- Photo Mode's **Look at camera** has priority over our eye edits: while it is on they wait, and come back when it
  is turned off (head and chest edits stay, the game turns them on top). With a full pose loaded (T / A pose, saved
  poses) the neck (and the spine, for the torso) is turned towards the camera: the face ends parallel to the camera's view. Edited eyes are absolute on the face rig, so expressions that roll
  the eyes (e.g. Whistle) no longer move them.
- **Joint limits** (lock icon, on by default): rotations stay within natural ranges (knees and elbows bend one way
  only, the spine, neck, wrists, palm and fingers within human ranges; a hand / foot pinned with IK still bends only as
  far as a wrist / ankle can). Dragging a value stops at the limit. Click the lock to turn them off for full control.
- **In hand**: one row per hand. Click a hand's item to open the item list beside the panel: click the search box
  to type (Enter takes the first result, Esc stops typing), filter with the **All / Weapons / Food / Other** tabs; recent items
  come first. An item put there by the Photo Mode pose itself (e.g. the gun of an *Action* pose) shows as
  "(pose)". **X** empties the hand; the **arrow** gives the pose's own item back. Weapons go in the right hand only (the game poses both hands around them); shards, braindances, quest and
  misc items are not listed. Move
  or rotate the item with the green hand prop marker. Items are saved with the pose; **Reset all** keeps them.
- While dragging a hand / foot (IK), the eyes' marker or Shift+dragging the hips / a held item, the **mouse wheel**
  moves it nearer or farther (the Photo Mode camera ignores the wheel meanwhile).
- **Undo / Redo** (also **Ctrl+Z** / **Ctrl+Y**): every drag, button or value change is one step (up to 100).
- **Mirror** (toggle): turning it on changes nothing; from then on, every edit of a left / right part makes the other
  side a mirror copy of that whole limb (arm, hand or leg, IK pin included), following it step by step. Eyes are not
  mirrored.
- **T pose / A pose**: replaces the Photo Mode pose with a neutral one to build a pose from scratch (face and eyes
  keep their edits); **Reset all** goes back.
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
