# Engine research notes (game 2.31, RED4ext 1.30)

These notes document how AdvancedPoseMod reaches the skeleton of a character.
Addresses are RVAs (relative to the image base `0x140000000`) and are only valid for 2.31;
the plugin never hardcodes them, it resolves them at runtime.

## Animation graph output node

- `animAnimNode_Output` (size `0x60`) is the final node of every animation graph.
- Its `PoseLink node` lives at `+0x48`; the connected input node pointer is at `+0x58`.
- vtable (2.31): `+0x2B54110`. Slot `0x128` is `Sample(this, ctx, a3, out)`.
  - Output::Sample (2.31: `+0x2C94F0`) tail-calls the input node's `Sample` (`vtable[0x128]`),
    or writes the reference pose when nothing is connected.
  - Prologue used as a safety signature: `40 53 48 83 EC 20 48 8B 49 58 49 8B D9`.
- The plugin finds the vtable at runtime by constructing an `animAnimNode_Output` through RTTI
  (`CClass::ConstructCls`) and reading the vtable pointer, so the hook survives patches
  as long as the slot index and prologue do not change.

### `out` (SampleOutput)

| Offset | Type | Meaning |
|-------:|------|---------|
| `+0x00` | `Pose*` | pose buffer |
| `+0x08` | `QsTransform*` | trajectory / root motion |

### Pose buffer

| Offset | Type | Meaning |
|-------:|------|---------|
| `+0x08` | `uint32` | bit count (validity bitset) |
| `+0x24` | `uint32` | bone transform count |
| `+0x28` | `uint32` | float track count |
| `+0x30` | `QsTransform*` | bone transforms, local space, stride `0x30` |
| `+0x38` | `float*` | float tracks |
| `+0x40`, `+0x48` | `uint32*` | bitsets |

Found in the "reset to reference pose" helper (2.31 `+0x2CE7EC`) and the
"copy reference pose" helper (2.31 `+0x2CF6AC`).

## Context -> entity mapping

Sampling contexts are pooled per worker thread (only ~16 distinct contexts in a scene), so a context must be
resolved on every call, never cached.

Found by the probe (2.31), for V's body graph (`root` component, 71 bones):

| Path | Points to |
|------|-----------|
| `ctx+0x48` -> `+0x118` | `entAnimatedComponent` being sampled |
| `ctx+0x40` -> `+0x00` | `animRig` (shared between entities, not unique) |
| `ctx+0x108`, `ctx+0x140` | `animRig` |
| `ctx+0x188` -> `+0xB8` | owning entity |

Call stack of the outermost `Output::Sample` (2.31): `+0x2CA36F +0x2CA19B +0x1E1AFB +0x328B35 +0x1D31F9 ...`

V's Photo Mode puppet has these animated components: `root` (71, main skeleton incl. fingers),
`deformations` (181), `face_rig` (344), `shadow` (56), `breasts` (20), 3x `hair_dangle`, one weapon/prop rig, and the
dangle rigs of some clothes (e.g. `tony_long_open_coat_dangle`, 17 bones). Female and male V use the same rig
resources (identical hashes and reference poses); only the extra components differ.

## Photo Mode

Hashes and `gamePhotoModeSystem` offsets come from PhotoModeEx (psiberx, MIT):

- `PhotoModeSystem::Activate` 2593396187, `Deactivate` 3043759683,
  `SpawnCharacter` 978529375, `ApplyPuppetTransforms` 2694650124
- Player list: `DynArray<PhotoModeCharacter>` at `+0x130`, element size `0x190`, puppet handle at `+0x00`,
  character type (uint32) at `+0x5C` (1 = female V, 2 = male V, 4 = NPC). V is slot 0; `+0x140` also points to it.

### Characters added in Photo Mode (1.1.0 probe)

- `+0x180` is the catalog of every character Photo Mode can add: `DynArray<PhotoModeCharacter>`, element size `0x190`,
  257 entries with a character pack installed (base game + mods registered through PhotoModeEx). Entries are
  type 4; the puppet handle stays empty until the character is spawned.
- `+0x1D0`, `+0x1D8`, `+0x1E0` point to the catalog entries of the (up to three) characters shown in the scene. Changing
  a character spawns the new entry and the slot pointer moves to it.
- `SpawnCharacter(system, character, transform, a4)` receives the catalog entry; the puppet is loaded asynchronously
  (handle empty at the call, components and rigs some frames later).
- `ApplyPuppetTransforms(system, list, group)` is called with the player list (group 0) and with the catalog
  (`+0x180`, group 1).
- Human NPCs (class `NPCPuppet`) use the same `root` (71), `deformations` (181) and `face_rig` (344) rigs as V (same rig
  resources: identical hashes), plus their own clothing / hair dangle rigs (6-136 bones). Heavy bodies add extra
  skeletons (e.g. `adam_smasher_extra_joints`, 85 bones; `leg_pistons`, 29 bones) on top of the standard ones.
- Non-humanoid characters have unrelated rigs: a quadruped (`Animated5678`, 75 bones: `spine_01_JNT`...,
  `frontLeg_*`, `backLeg_*`, `tail_*`) and props of class `gameObject` (`Animated8050`, 2 bones). Not supported yet.

## Rig

`anim::Rig` (resource): `parentIndices` at `+0x40`, `referencePoseLS` at `+0x48`, `boneNames` at `+0x50`
(see RED4ext.SDK `animRig.hpp`).

## Photo Mode menu

The menu is built natively but every widget step goes through script callbacks of
`gameuiPhotoModeMenuController` (`OnAddMenuItem`, `OnAddingMenuItemsFinished`, `OnSetupScrollBar`,
`OnSetupOptionSelector`, `OnForceAttributeVaulue`, ...). Pages: 0 camera, 1 time/weather, 2 pose, 3 NPC, 4 lights,
5 effects, 6 color, 7 stickers, 8 load/save.

Value changes of list items call the native `OnAttributeUpdated(attribute, value, apply)`. Its handler (2.31
`+0x27D7740`) parses the arguments and calls `Forward(this + 0x130, attribute, value, apply)` (2.31 `+0x27D62E8`).
The plugin locates that call at runtime (pattern `48 8D 8F 30 01 00 00` + `E8`) and drops keys 7700-7799,
so AdvancedPoseMod's own items never reach the game's Photo Mode system.

## Twist helper bones (deformations skeleton)

The `deformations` component (181 bones) holds CDPR's twist helpers, all placed on the bone line (local X):

| Helper | Parent | Position along the bone |
|---|---|---|
| `l/r_Wrist_0/1/2_JNT` | ForeArm | 0.33 / 0.67 / 0.98 |
| `l/r_SHL_0/1_JNT` | Arm | 0.33 / 0.67 |
| `l/r_thigh_0/1_JNT` | UpLeg | 0.33 / 0.67 |
| `l/r_calf_0_JNT` | Leg | 0.75 |

Muscle correctives (`*_mscl_JNT`, `*_out_JNT`) hang below them. (A 0.4.1 attempt to drive the helpers from the hook
never ran: see "Component skeletons inherit the main pose".)

## Photo Mode sliders

Sliders with a fractional step (0.01) only reported changes on release in testing; distances are therefore shown in
whole centimeters.

## Component skeletons inherit the main pose (0.7.3)

Measured with the game's own attachment slots (`SlotComponent.GetSlotTransform`: Head, Chest, Hips, RightHand
match the main skeleton's bones exactly when nothing is edited): applying the same-named edits to every hooked
skeleton multiplied them. A chest bent by 8 degrees was drawn bent by ~55 degrees; Spine3 exists in 8 of the 8
hooked skeletons (root, shadow, face_rig, 3x hair_dangle, breasts, Animated4171), which take the shared bones from
the already edited pose one after the other. Since 0.7.3 only the main skeleton is edited (plus linked bones such
as the face rig's eyeballs).

The `deformations` skeleton (181 bones, twist/muscle helpers) is never sampled through `animAnimNode_Output`, so
the 0.4.1 twist-helper code never ran and was removed.

## Posing the added characters (1.2.0)

- Each scene slot (`+0x1D0`, `+0x1D8`, `+0x1E0`) points to a catalog entry; the puppet handle is at `+0x00` and the
  type at `+0x5C` (4). The puppet loads asynchronously: its components appear some frames after the spawn.
- A human character has a `root` skeleton of 71 bones like V; the plugin registers its components as targets with
  character index 1-3 (slot + 1). Every character has its own pose state; a new puppet in a slot clears that state.
- The NPC page options apply to the character selected in that page. Its selector is attribute 68 (0-2, the scene
  slot); after a change the page sends the options of that character again (56-66, 78-82). The script callback
  `OnSetSetSelectedNpc` is not called when the selector changes.
- An added character's body graph samples 72 transforms for its 71-bone rig (V's: 71): the rig's bones come first,
  in rig order, and one extra transform follows. The pose header keeps the rig's bone count at `+0x00`.

## Photo Mode attributes read by the plugin

Values reach the native `OnAttributeUpdated` forward (see "Photo Mode menu"); the plugin only reads them.

| Attribute | Meaning | Values |
|---:|---|---|
| 15 | Look at camera | 0 off, 1 camera, 2 custom |
| 74 | Look at camera target | 0 torso, 1 head, 2 eyes |
| 28 | Facial expression | index in the menu list; 0-14 base game, mods append after |
| 58 | Look at camera of the selected added character | as 15 |
| 78 | Look at camera target of the selected added character | as 74 |
| 68 | Character selected in the NPC page (0-2 = scene slot) | index |
| 79-81 | Custom look-at values of the selected added character (V: 75-77) | |

## Eyelids follow the eyes (1.1.0)

Measured with the EyelidProbe (face rig bones as the game animated them, Look at camera on the eyes, camera moved up /
down / sideways, every base game expression). The eye bones `l/r_J_eye_JNT` turn at most about 30 degrees down,
21 up and 40 sideways (turn vector in the parent space: x sideways, z up / down).

- The game moves 44 eyelid bones of each eye (`*_eye_lid_up_*`, `*_eye_lid_dn_*`; lashes and some rows only follow
  their parents) almost linearly with the eye's pitch, with a different slope looking down and up. Neutral
  expression, upper lid roots: 92-100 % of the eye turn down, 75 % up; lower lid roots: 58 % down, 35 % up. Sideways
  matters little except at the corners.
- Each expression keeps a share of that movement, per eye and per lid: expressions that hold the eyelids (squint,
  wide eyes, wink, nearly closed) keep 5-40 % of the upper lid movement; most others 70-100 %. The share is a property
  of the expression; it cannot be read from a single frame (an eyelid's position mixes the expression's shape and
  the gaze).
- `tools/eyelid_follow_fit.py` turns probe logs into `src/Pose/EyelidFollowData.inc` (per bone slopes, per expression
  shares). Predicting "eyes straight -> 30 degrees down" from the table matches the game within 2-6 % of the lid
  movement.
