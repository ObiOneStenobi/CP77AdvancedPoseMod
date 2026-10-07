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
`deformations` (181), `face_rig` (344), `shadow` (56), `breasts` (20), 3x `hair_dangle`, and one weapon/prop rig.

## Photo Mode

Hashes and `gamePhotoModeSystem` offsets come from PhotoModeEx (psiberx, MIT):

- `PhotoModeSystem::Activate` 2593396187, `Deactivate` 3043759683,
  `SpawnCharacter` 978529375, `ApplyPuppetTransforms` 2694650124
- Player list: `DynArray<PhotoModeCharacter>` at `+0x130`, element size `0x190`, puppet handle at `+0x00`.

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

Muscle correctives (`*_mscl_JNT`, `*_out_JNT`) hang below them. The game computes the helpers before our Output hook,
so `PoseProcessor` adds the twist part of our own change: forearm helpers take a share of the added hand twist,
upper arm / thigh helpers cancel part of the added arm / thigh twist. Calf helpers take half of the added foot twist.

## Photo Mode sliders

Sliders with a fractional step (0.01) only reported changes on release in testing; distances are therefore shown in
whole centimeters.
