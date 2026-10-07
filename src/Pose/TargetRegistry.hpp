#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// A "pose target" is a character whose skeleton can be overridden (currently: V's Photo Mode puppet).
// The registry is written on the game thread (Photo Mode hooks) and read from animation worker threads.
namespace APM::Pose
{
struct Target
{
    uintptr_t entity = 0;            // ent::Entity*
    uintptr_t animatedComponent = 0; // ent::AnimatedComponent*
    uintptr_t rig = 0;               // anim::Rig* (resource)
    uint32_t photoModeSlot = 0;      // index in the Photo Mode player list
    uint32_t characterType = 0;      // 1 = PlayerFemale, 2 = PlayerMale, 4 = NPC
    std::vector<uint64_t> boneNames; // CName hashes, rig order
    std::vector<int16_t> parents;    // parent index per bone (-1 for root)
    std::string debugName;
};

namespace TargetRegistry
{
void Set(std::vector<Target> aTargets);
void Clear();
size_t Count();

// Copy of the target that owns the given entity or animated component pointer.
bool FindByPointer(uintptr_t aPointer, Target& aOut);

// Fast, allocation-free check used in the animation hot path.
bool ContainsPointer(uintptr_t aPointer);

// Pointers (entities, animated components, rigs) the research probe looks for.
size_t GetProbePointers(std::array<uintptr_t, 64>& aOut);

// Resolve a bone index by name for the target that owns the pointer. Returns -1 if not found.
int32_t FindBoneIndex(uintptr_t aPointer, uint64_t aBoneNameHash);

// True if any registered target has a rig with exactly this many bones (probe heuristic).
bool HasBoneCount(uint32_t aCount);
} // namespace TargetRegistry
} // namespace APM::Pose
