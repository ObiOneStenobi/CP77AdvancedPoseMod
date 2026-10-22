# AdvancedPoseMod

Bone-by-bone posing with inverse kinematics for Cyberpunk 2077 Photo Mode.

V and the characters added in Photo Mode can be posed bone by bone from the **Pose editor** panel and with gizmos on
the joints (rotation, IK for hands and feet, hand presets, eyes, items in hand), and whole poses can be saved, loaded
and shared.

## Download

- [Nexus Mods](https://www.nexusmods.com/cyberpunk2077/mods/35013)
- [GitHub Releases](https://github.com/ObiOneStenobi/CP77AdvancedPoseMod/releases): `AdvancedPoseMod-<version>.zip`

Install with Vortex, or extract the zip into the game folder (the one containing `bin`).

## Requirements

- Cyberpunk 2077 2.5 (PC); for 2.31 use AdvancedPoseMod 1.3.0
- [RED4ext](https://docs.red4ext.com) with 2.5 support
- [redscript](https://github.com/jac3km4/redscript)
- [Codeware](https://github.com/psiberx/cp2077-codeware) (interface translations)

Windows only so far: Linux / Steam Deck (Proton) has not been tested.
Any screen shape is supported (16:9, 16:10, ultrawide 21:9 / 32:9): the gizmos follow the game window's size.

To build from source:

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
  Below them icon buttons (rest the mouse on any button for a moment to see a tooltip above it): **Undo**, **Redo**,
  **Mirror**, **Joint limits**, **Auto IK**, and at the far right **Reset all** (click twice). **?** opens the info
  window (marker legend and shortcuts).
- **Reset** (next to the selected part's name) undoes every edit of that part; a pinned hand / foot goes back to where
  the pose puts it (the pin stays). **Right-click a marker** does the same for its part. A small blue dot marks the
  edited parts and the tabs that hold them (a loaded pose or T / A pose alone does not count). Choosing an IK limb also selects that hand / foot above. The IK limb buttons get an orange frame,
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
  Poses in subfolders of `poses` are listed too, as `Folder/Name` (pose packs can ship in their own folder, so they
  never overwrite each other); the mod saves its own poses in `poses` itself, and renaming keeps a pose in its folder.
  Saved expressions (`expressions`) are read from subfolders the same way.
- **Hand poses** (**Hands** button on the Poses section): a second library, `hands\*.json`, with the palm and fingers
  of one hand. Open the tab of a hand (or of its arm), then **Save new**, **Overwrite** or **Load** work on that hand;
  one file poses either hand (the rig is mirrored). Rename, delete, pages and subfolders work as for the poses.
- Every time Photo Mode opens it starts from the game's pose: the edits, held items, Mirror and undo history of the
  previous session are cleared. Save a pose to keep it. While there are edits, leaving Photo Mode asks for
  confirmation (as the game does for its own options).
- **Characters**: when characters are added in the scene (Photo Mode NPC page), a row with the selector `< name >`
  appears under the title (names come from the game's character records): the panel, the gizmos, undo / redo and the
  items in hand work on the character shown there. Every character keeps its own pose; replacing a character in a slot starts it from the game's pose. Saved poses work on
  any of them (V and human characters share the same skeleton). Look at camera works for them as for V (Photo Mode
  NPC page). Only human characters can be posed (animals and objects have other skeletons). The NPC page's facial
  expressions work only on some characters (e.g. Wakako): that is the game, the same happens without this mod.
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
- Markers that cover each other on screen (e.g. the fingers of two hands one over the other): the nearest is drawn on
  top and takes the click. Up close the ones behind turn fainter, the more the farther behind (from 1 to 5 cm);
  they stay selectable. From afar nothing fades.
- **Heels** (Left / Right leg tabs; close-up markers like the fingers): the pivot of the front of the foot, turning one
  lifts or lowers the toes around the heel.
- **Gizmo mode** (button next to *Gizmos*: **Direct**, **Rotate**, **Move**): Direct drags the markers (as described below); Rotate puts on the selected part
  three rings (red / green / blue = the panel's *forward / back*, *sideways* and *twist*) and an outer white ring
  that turns it as seen from the camera; drag a ring to turn the part around that axis. Move shows arrows that move the hips, held
  items and hands / feet (IK). **Local / Global** (next to it) switches between the part's own axes (they turn with it) and the character's. In Rotate / Move the
  markers only select (click one to put the gizmo on it). The back half of a ring is faint and cannot be picked.
- Drag a white marker to rotate the part: the end of the bone follows the cursor. The head turns as seen from the
  camera instead (left/right around the screen's vertical axis, up/down around its horizontal axis).
- **Ctrl+drag** any marker to twist the part around the bone itself.
- **Alt+drag** any marker to rotate the part freely, like a ball under the cursor: left / right turns it around the
  camera's vertical axis, up / down around its horizontal axis (as the head always does).
- Drag an orange marker (hands and feet) and it follows the cursor on a plane facing the camera (IK, pinned
  automatically). **Shift+drag** it to rotate the hand / foot like any other bone (its tip follows the cursor,
  with a calmer speed); Alt+drag turns it freely. For the hands, *forward / back* bends the wrist towards the palm /
  the back of the hand and *sideways* towards the thumb / the little finger, as for the feet. A pinned
  hand / foot turns only as far as the wrist / ankle can (joint limits). A straight leg / arm bends the natural way
  (knee forward, elbow back).
- The pin row (**Off / World / Body**, under the limb buttons) sets the pin: *world* keeps the hand / foot where it is whatever the body does;
  *body* makes it follow the shoulder (hips) it hangs from, keeping the arm's (leg's) pose while the torso turns.
  Turning the pin on, or switching between world and body, leaves the hand / foot where it is.
- While a limb is pinned, dragging its upper arm / forearm (thigh / shin) marker turns the elbow (knee) around the
  shoulder -> hand line.
- The orange marker in front of the face is the point both eyes look at: drag it and the eyes converge on it.
- Shift / Ctrl / Alt can be pressed or released during a drag: the action changes on the fly, keeping what was done.
- Close-up gizmos (camera near the hand): palm bones (cup or spread the hand) and thumb base.
- **Hand prop** (green marker, also in the hand tab): the point a held weapon / prop hangs from. Drag to rotate it,
  **Shift+drag** to move it. Dragging the hips moves the whole body (**Shift+drag** rotates them as seen from the camera, like the head); while the hips are selected a grey
  diamond shows V's root (where Photo Mode placed her), with a line to the hips: saved poses keep the hips
  position relative to that point. The eyes turn at most 30 degrees from where the pose points them; with **Eyes (both)** the
  third row is their **focus distance** (nearer = more converged).
- **Auto IK** (magnet icon, on by default): dragging a hand / foot moves it with IK and pins it, Shift+drag rotates
  it. Turned off, dragging rotates the hand / foot like any other bone; Shift+drag moves it with IK only if that
  limb is already pinned in the IK row. Turning a pin off keeps the arm / leg where the IK put it (Reset undoes it).
  With joint limits on, a pinned limb also stops where the hip / shoulder and knee / elbow can no longer follow.
- **Eyes**: the Head tab has one **Eyes** part; its **Both / Left / Right** switch picks one look marker for both eyes
  or one marker per eye.
- The hips marker is violet: it is V's root and moves the whole body.
- **Hand presets** (in the Left hand / Right hand tabs, and under the hand part of an arm tab): Open, Relaxed, Fist, Point, Grip set all the fingers of that
  hand at once (undo and Mirror work as usual); then refine with the finger markers.
- Photo Mode's **Look at camera** has priority over our eye edits: while it is on they wait, and come back when it
  is turned off (head and chest edits stay, the game turns them on top). With a full pose loaded (T / A pose, saved
  poses) the neck (and the spine, for the torso) is turned so the face looks at the camera.
  Edits of the hips or the back no longer turn the face away (the turn is applied after them), and IK pins are
  measured on the pose without the turn: a world pin stays put, a body pin follows the turned chest.
  With Torso or Head the eyes also follow the camera (up to 30 degrees from where the face looks), also when the
  head has reached its limit. Edited eyes are absolute on the face rig, so expressions that roll
  the eyes (e.g. Whistle) no longer move them. While Look at camera is on, the bones it turns (eyes; head and neck;
  the back and chest with Torso) are grey and cannot be edited. With **Custom** the game turns them towards its own
  point: they keep the game's turn (a loaded full pose leaves them alone too).
- **Eyelids follow the eyes**: when the eyes are turned (eye markers or Look at camera), the eyelids move as the game
  moves them for its own gaze (the upper lid lowers when looking down), by the share the current Photo Mode expression
  allows: expressions that hold the eyelids (squint, wide eyes, wink, closed eyes) keep them almost still. Expressions
  added by other mods follow like the neutral one. Added characters use their own expression (NPC page).
- **Saved poses folder**: the folder icon on the Poses section opens the folder of the saved poses, to share the
  files (in full screen the game is minimized). Face controls and saved expressions are planned for a later version.
- **Joint limits** (lock icon, on by default): rotations stay within natural ranges (knees and elbows bend one way
  only, the spine, neck, wrists, palm and fingers within human ranges; a hand / foot pinned with IK still bends only as
  far as a wrist / ankle can). Dragging a value stops at the limit. Click the lock to turn them off for full control.
- **In hand**: one row per hand. Click a hand's item to open the item list beside the panel: click the search box
  to type (Enter takes the first result, Esc stops typing), filter with the **All / Weapons / Food / Other** tabs; recent items
  come first. An item put there by the Photo Mode pose itself (e.g. the gun of an *Action* pose) shows as
  "(pose)". **X** empties the hand; the **arrow** gives the pose's own item back. Weapons go in the right hand only (the game poses both hands around them); shards, braindances, quest and
  misc items are not listed. Move
  or rotate the item with the green hand prop marker. Items (and where they are held) are saved with the pose; **Reset all** keeps them.
- While dragging a hand / foot (IK), the eyes' marker or Shift+dragging the hips / a held item, the **mouse wheel**
  moves it nearer or farther (the Photo Mode camera ignores the wheel meanwhile).
- **Undo / Redo** (also **Ctrl+Z** / **Ctrl+Y**): every drag, button or value change is one step (up to 100).
  Every character has its own history: undo works on the character selected in the header, and switching to another
  character and back keeps it.
- **Mirror** (toggle): turning it on changes nothing; from then on, every edit of a left / right part makes the other
  side a mirror copy of that whole limb (arm, hand or leg, IK pin included), following it step by step. Eyes are not
  mirrored.
- **T pose / A pose**: replaces the Photo Mode pose with a neutral one to build a pose from scratch (face and eyes
  keep their edits); **Reset all** goes back.
- Moving a whole finger keeps the extra bend of its segments.

Developers: `cet/` holds a debug window for Cyber Engine Tweaks with raw bone access (not part of the release
package). `config.ini` `[Debug] Log=1` writes detailed diagnostics to the log, `[Probe] DumpRig=1` writes the skeletons
to `probe\`.

Pose files are written to `<game>\red4ext\plugins\AdvancedPoseMod\poses\<name>.json`.

## Known limitations

- With **Photo Mode UI Improvements**, Judy's body cannot be posed (that mod changes how her body is animated; she is
  also shown offset without this mod). Other characters work with it.
- Rogue (white hair): a piece of her jacket stays still in space. The game does it, also without this mod.

- Characters added in Photo Mode can be posed when they are human (V's skeleton); animals and objects have unrelated
  skeletons and stay unsupported. Their eyelids follow edited eyes like the neutral expression.
- The eyelids follow edited eyes with the share measured for each of the 15 base game expressions; expressions added
  by other mods follow like the neutral one (a closed-eye mod expression may open slightly when the eyes turn).
- No face controls or saved expressions yet; the face keeps the Photo Mode expression.
- No automatic feet on the ground: place the feet with IK.
- Built for game version 2.5; on another version the game still starts, but the mod stays off and the panel reports
  an unsupported version until the mod is updated. `config.ini` `[Compatibility] AllowUnknownGameVersion=1` tries it
  anyway (at your own risk: if the update moved what the mod reads, the game can crash).

## Translations

The panel is in English; translations are separate mods that need no change to AdvancedPoseMod. Every text has a
key, and the panel shows the translation that Codeware's localization system has for the game's interface language
(English for the texts a translation leaves out).

To translate:

1. Copy [`tools/translation_template.reds`](tools/translation_template.reds) to
   `r6\scripts\AdvancedPoseMod_<language>\Translation.reds` (e.g. `AdvancedPoseMod_it`).
2. Rename the module and the two classes (`xx` -> your language) and set the language code in `GetPackage`
   (`it-it`, `fr-fr`, `de-de`, `es-es`, `pl-pl`, `pt-br`, `ru-ru`, `zh-cn`, ...).
3. Translate the second text of every line, keeping `{0}` (a name goes there).
4. Reopen Photo Mode to see it; publish the file as a mod that requires AdvancedPoseMod and Codeware.

Texts on fixed-size buttons shrink to fit when they are longer than the English ones. The template lists every key
in use; developers regenerate it with `python tools/gen_loc_template.py` after changing a text.

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

Hand pose files (`hands\*.json`):

```json
{
  "format": "AdvancedPoseMod.Hand",
  "version": 1,
  "bones": { "HandIndex1": [0.0, 0.0, -0.2, 0.98], "InHandThumb": [0.0, 0.1, 0.0, 0.99] }
}
```

- `bones`: palm and finger bones without the `Left` / `Right` prefix, each a local rotation quaternion `[i, j, k, r]`
  from the bone's reference (T pose) rotation. Missing bones are left as they are.

## Layout

```
src/Core      runtime, logging, config, guarded memory reads
src/Game      engine hooks (Photo Mode, animation graph)
src/Math      quaternion helpers
src/Pose      pose targets, override layer, pose files (later: IK)
src/Scripting native functions exposed to redscript / CET (APM_*)
scripts/      redscript: native declarations + pose editor panel and gizmos, interface texts (Localization.reds)
src/Ui        menu state, body part catalog
cet/          developer debug window for Cyber Engine Tweaks
data/         files shipped next to the DLL
docs/         research notes
tools/        developer scripts, translation template (gen_loc_template.py builds it)
```

## Credits

- RED4ext / RED4ext.SDK – WopsS and contributors
- Photo Mode function hashes and structure offsets – psiberx (PhotoModeEx, MIT)
- nlohmann/json – Niels Lohmann (MIT)

## License

MIT – see `LICENSE`. Third-party components keep their own licenses, listed in `THIRD_PARTY_NOTICES.md`.
