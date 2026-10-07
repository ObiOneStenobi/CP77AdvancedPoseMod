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

## Context -> entity mapping (open question)

`ctx` does not obviously reference the entity. The phase 0 probe scans `ctx` (depth 2, `0x200` bytes per level)
for a pointer to the Photo Mode puppet entity or its `entAnimatedComponent` and logs the path it finds.

## Photo Mode

Hashes and `gamePhotoModeSystem` offsets come from PhotoModeEx (psiberx, MIT):

- `PhotoModeSystem::Activate` 2593396187, `Deactivate` 3043759683,
  `SpawnCharacter` 978529375, `ApplyPuppetTransforms` 2694650124
- Player list: `DynArray<PhotoModeCharacter>` at `+0x130`, element size `0x190`, puppet handle at `+0x00`.

## Rig

`anim::Rig` (resource): `parentIndices` at `+0x40`, `referencePoseLS` at `+0x48`, `boneNames` at `+0x50`
(see RED4ext.SDK `animRig.hpp`).
