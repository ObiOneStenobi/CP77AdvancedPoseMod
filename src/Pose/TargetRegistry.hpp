#pragma once

#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"

#include <RED4ext/Handle.hpp>
#include <RED4ext/ISerializable.hpp>

#include <array>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

// A "pose target" is a character whose skeleton can be overridden (currently: V's Photo Mode puppet).
// The registry is written on the game thread (Photo Mode hooks) and read from animation worker threads.
namespace APM::Pose
{
// A bone of another skeleton that must follow an override (e.g. root "LeftEye" -> face_rig "l_J_eye_JNT").
// The rotation is converted between the two bones' frames: dst = pre * src * post.
struct LinkedBone
{
    uint64_t sourceBone = 0; // bone name hash the user edits
    uint32_t targetIndex = 0; // bone index in this target
    Math::Quat pre{};
    Math::Quat post{};
    Math::Quat sourceReferenceLocal{}; // reference local rotations of both bones (absolute eyes)
    Math::Quat targetReferenceLocal{};
};

struct Target
{
    uintptr_t entity = 0;            // ent::Entity* (identity only: may point to a destroyed object, see entityRef)
    uintptr_t animatedComponent = 0; // ent::AnimatedComponent*
    // Weak references taken while the objects were alive: they turn empty when the game destroys them (e.g. when
    // Photo Mode closes), so scripts never get an object that is gone.
    RED4ext::WeakHandle<RED4ext::ISerializable> entityRef;
    RED4ext::WeakHandle<RED4ext::ISerializable> componentRef;
    uintptr_t rig = 0;               // anim::Rig* (resource)
    uint32_t photoModeSlot = 0;      // index in the Photo Mode player list
    uint32_t characterType = 0;      // 1 = PlayerFemale, 2 = PlayerMale, 4 = NPC
    std::vector<uint64_t> boneNames; // CName hashes, rig order
    std::vector<int16_t> parents;    // parent index per bone (-1 for root)
    std::vector<Math::Quat> refRotationsMS; // reference pose rotations in model space
    std::vector<Math::Quat> refRotationsLS; // reference pose rotations in local space
    std::vector<Math::Vec3> refTranslationsLS; // reference pose translations in local (parent) space
    std::vector<LinkedBone> linkedBones;
    std::string debugName;
};

namespace TargetRegistry
{
void Set(std::vector<Target> aTargets);
void Clear();
size_t Count();

// Copy of the target that owns the given entity or animated component pointer.
bool FindByPointer(uintptr_t aPointer, Target& aOut);

// Fast, allocation-free check used in the animation hot path (entities and animated components only).
bool ContainsPointer(uintptr_t aPointer);

// Pointers (entities, animated components, rigs) the research probe looks for.
size_t GetProbePointers(std::array<uintptr_t, 64>& aOut);

// Strong handle to the entity or animated component known by that pointer; empty if unknown or already destroyed.
RED4ext::Handle<RED4ext::ISerializable> LockObject(uintptr_t aPointer);
// Strong handle to the main skeleton ("root") component of a character (0 = V, 1-3 = added characters).
RED4ext::Handle<RED4ext::ISerializable> LockMainComponent(uint32_t aCharacter);

// Calls aVisitor with the target that owns the pointer, under a shared lock (no copy). Returns false if not found.
bool VisitTarget(uintptr_t aPointer, const std::function<void(const Target&)>& aVisitor);

// Calls aVisitor with every target, under a shared lock (diagnostics; not for the animation hot path).
void ForEach(const std::function<void(const Target&)>& aVisitor);

// Resolve a bone index by name for the target that owns the pointer. Returns -1 if not found.
int32_t FindBoneIndex(uintptr_t aPointer, uint64_t aBoneNameHash);

// True if any registered target has a rig with exactly this many bones (probe heuristic).
bool HasBoneCount(uint32_t aCount);

// Player-friendly rotation axes of a bone of V's main skeleton, expressed in the bone's local space:
//   bend1  = bend forward/back (or swing forward/back for horizontal limbs)
//   bend2  = bend sideways (or raise/lower for horizontal limbs)
//   twist  = along the bone
// Returns false if the main skeleton is not available or the bone is unknown.
bool GetBoneAxes(uint64_t aBoneHash, Math::Vec3& aBend1, Math::Vec3& aBend2, Math::Vec3& aTwist);
// Same, for a given character's main skeleton (0 = V, 1-3 = added characters) instead of the selected one.
bool GetBoneAxes(uint32_t aCharacter, uint64_t aBoneHash, Math::Vec3& aBend1, Math::Vec3& aBend2, Math::Vec3& aTwist);

// Direction, in the bone's local space, that points where the face looks in the skeleton's reference pose.
// Used for the eyes' gaze. Returns false if the main skeleton is not available or the bone is unknown.
bool GetBoneReferenceForward(uint64_t aBoneHash, Math::Vec3& aOut);

// Reference ("bind") pose of V's main skeleton - a T pose.
struct ReferencePose
{
    std::vector<uint64_t> boneNames;
    std::vector<int16_t> parents;
    std::vector<Math::Quat> localRotations;
    std::vector<Math::Vec3> localTranslations;
    std::vector<Math::Quat> modelRotations;
    uintptr_t rig = 0; // anim::Rig* it was read from
};
bool GetReferencePose(ReferencePose& aOut);
bool GetReferencePose(uint32_t aCharacter, ReferencePose& aOut);

// Rig of a character's main skeleton (anim::Rig*, identity only), 0 if not available.
uintptr_t GetMainRig(uint32_t aCharacter);

// Bone names of V's main skeleton ("root" component), for UIs. Empty if not available.
std::vector<uint64_t> GetMainSkeletonBones();
} // namespace TargetRegistry
} // namespace APM::Pose
