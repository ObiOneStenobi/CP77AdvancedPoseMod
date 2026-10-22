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
The plugin locates that call at runtime (pattern `48 8D 8F xx 01 00 00` + `E8`, any displacement 0x100-0x1FF) and
drops keys 7700-7799, so AdvancedPoseMod's own items never reach the game's Photo Mode system. In 2.5 the callback
moved to `this + 0x140` (handler `+0x2D6FD44`, forward `+0x2D6E80C`); the forward function itself is unchanged.

The menu controller and its root widget live for the whole game, not one Photo Mode session (`OnInitialize` runs
once). The root draws its children backward: the first child is on top and takes the clicks first. Other Photo Mode
mods (Photo Mode UI Improvements) move their layers to the front when they add them; their invisible containers then
swallow clicks over our gizmos and buttons (the press reaches no widget). Since 1.3.0 the pose editor layer is kept
first. The game hides the root's children while Photo Mode opens and shows them again.

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
- Forcing the animation LOD of added characters (`clientForcedLod`) is not needed: their body pose is complete (see
  the extra transform above).
- Names (1.3.0): many Photo Mode character records inherit the player's display name (`LocKey#48683`, "V"), so
  `GetDisplayName()` is useless for them. The name comes from the record's `DisplayName` / `FullDisplayName` (when not
  the player's), else from the record id (`Character.<Name>_Photomode_Puppet`; readable with TweakXL's debug names).
- Eyes (1.3.0): in the game's poses the main skeleton's `LeftEye` / `RightEye` stay at their reference (0 degrees) for V
  and every added character measured, Jackie included. Edited eyes of every character must start from that
  character's own pose: 1.2.0 took the selected character's for all of them (twisted NPC eyes, V's eyes turning black).

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
| 5 / 6 | V's Photo Mode pose: category / pose in that category | index |
| 56 | Facial expression of the character selected in the NPC page | index (0-15) |
| 57 | Pose of the character selected in the NPC page | index |
| 55 | NPC page: scene slot a character is being added to | 0-2 |
| 63 | NPC page: which character is added (list index) | index |
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

## Known character-specific issues (not handled)

Found with the diagnostics of 1.2.0 (game 2.31). Every added human character so far has the same 71-bone rig as V
(same reference pose for the eyes and the weapon bones) and the same hand item slots (`Item_Attachment_Slot`, bound to
`root`: `WeaponRight` / `WeaponLeft` -> bones of the same name, no offset).

- **Jackie: eye gizmos point the wrong way.** The rig's eye reference is the same as V's, and her animated
  `LeftEye` / `RightEye` measured 0 degrees from the reference (1.3.0), so the animation does not turn them: the cause
  is elsewhere (possibly the 1.2.0 eye bug above, to be checked again). Possible fix: take the gizmo's gaze from the
  `face_rig` eyes.
- **Dexter DeShawn: right hand item far from the hand (near the feet), it still follows the hand.** `WeaponRight`
  (child of `RightHand`, reference local translation (-0.087 0.042 0.021)) is animated far from the hand by his poses.
  Possible fix: when the animated `WeaponRight` / `WeaponLeft` is more than ~20 cm from its hand, start from the
  reference local transform (the user's item offset applies on top as now).
- **Look at camera on the torso** also turns the hips and thighs (`Hips`, `LeftUpLeg`, `RightUpLeg`) by ~4-10 degrees,
  depending on the camera (V and added characters). `RemoveGameLookAt` remembers and puts back those bones too, and
  tells a new Photo Mode pose apart by its pose selector (attributes 5 / 6, 57), not by the bones.
- **NPC page facial expressions (attribute 56) show only on some characters** (Wakako yes, most others no), also
  without the mod: the game. The eyelids still follow the expression chosen there.
- **Rogue (white hair): a piece of the jacket stays still in space**, also with the pose off and without other Photo
  Mode mods: the game. Her jacket skeleton (`Animated2476`, 55 bones: `dyng_*_jacket_01-05` chains on Hips / Spine /
  Spine1-3) also holds `l_thigh_0_JNT` / `r_thigh_0_JNT`, twist helpers that only the `deformations` skeleton animates;
  in the jacket's copy nothing moves them.
- **Judy with Photo Mode UI Improvements (1.3.0): her body cannot be posed.** Her `root` skeleton is never sampled
  through `animAnimNode_Output` while that mod is installed (it replaces the photo mode characters' entities and body
  graph); without it she is posed like the others. Her other skeletons (hair, overalls `dangle_setup`, face) are
  sampled normally.
- **Adding a character resets V's Look at camera target to the head** (the game sends attribute 74 = 1 just before the
  new character appears, and the torso option disappears from the menu until Look at camera is turned off and on
  again). Game behaviour: the plugin only reads these attributes.

## Game 2.5 (1.4.0)

Compared offline with 2.31 (executables, `cyberpunk2077_addresses.json`, `final.redscripts`):

- Game file version 3.0.88.46235 (2.31: 3.0.80.51928). `CheckGameVersion` accepts both.
- The four Photo Mode hashes (`Activate`, `Deactivate`, `SpawnCharacter`, `ApplyPuppetTransforms`) exist in the 2.5
  address file (same secondary hashes).
- `animAnimNode_Output::Sample`: same prologue (2.5 `+0x3AACCC`), still vtable slot `0x128`; the neighbouring slots
  look the same, and the 12 vtables sharing that function are in the same order.
- `gamePhotoModeSystem`: the functions using the player list (`+0x130`, size `+0x13C`), the NPC list (`+0x180`),
  the character stride `0x190` and the scene slots (`+0x1D0`) are byte-for-byte the same; `CharacterType` `+0x5C`
  is still read by `SpawnCharacter`. Other fields of the system moved by `0x10` (`+0x170` -> `+0x180`, ...): not used.
- `gameuiPhotoModeMenuController`: its forward callback moved from `+0x130` to `+0x140` (see Photo Mode menu).
- Scripts: redscript 0.5.31 cannot read the 2.5 bundle (header flags `2` instead of `0`, then "failed to fill whole
  buffer"); 0.5.32 (22/10/2026) can. Against the 2.5 bundle the mod's scripts compile without errors; Codeware
  (`613a1cb` and the 19/10/2026 head) does not yet (classes of its callback system now clash with the game's,
  `TextInput` methods missing): a Codeware update for 2.5 is needed.
- Since 1.4.0 the entity / component / rig fields come from the reflection where it has them (`Game/EngineLayout`):
  `entIComponent.name`, `entAnimatedComponent.rig`, `animRig.boneNames` and `animRig.tags` (the native
  `parentIndices` / `referencePoseLS` sit right before `boneNames`, the rig hash right before `tags`). The entity's
  component array is not reflected: the SDK offset `0xA0` is checked on the first entity and searched for if wrong.
  Offsets that differ from the SDK are logged (`Layout: ...`).
- Still to check in game: the animation context path (`ctx+0x48 -> +0x118`, probed again when wrong), the Photo Mode
  attribute numbers read by the plugin (15, 74, 28, 56, 68: Debug log), the rig dumps (`DumpRig=1`).

## Vehicles (study, 1.4.0)

- Photo Mode in a vehicle uses the game's ride poses (`PhotoModePoses.action____car_*_ride`,
  `action____sportbike*_ride`; Photo Mode Unlocker makes them selectable anywhere). Bike pose packs (e.g. asgard bike
  poses) are ordinary standing animations of a seated body: the vehicle comes from elsewhere.
- In third person the Photo Mode puppet is spawned as usual and placed on the seat: the plugin's path (player list,
  animation graph output, component local-to-world for the gizmos) should not change. To check: the pose actually
  applied on top of the ride animation, the IK world pin (the puppet is attached to the vehicle), the hips move.
- `photo_mode.general.onlyFPPPhotoModeInPlayerStates` / `onlyFPPPhotoModeForStatusEffectsWithTags` force first person
  in some states (Photo Mode Unlocker empties them). In first person there may be no puppet: then nothing to pose.
- Test plan (Debug Log=1, DumpRig=1): car driver, car passenger, bike; Photo Mode third person; note whether markers
  show, a rotation applies, a T pose loads, the IK world pin holds, and what the log reports for V's targets.

