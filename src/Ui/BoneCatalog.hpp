#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Player-facing list of posable body parts of V's main skeleton.
// One entry can drive several bones (both eyes, the three segments of a finger).
namespace APM::Ui
{
enum class EntryMode
{
    Single, // one bone
    Mirror, // same rotation on every bone (e.g. both eyes)
    Curl,   // finger: "bend" goes to every segment, the other axes only to the first one
    FingerSegment, // one outer finger segment (finger axes); gizmo only, not listed in the panel
    Group,  // physics bones moved as one piece (a breast), through a virtual bone (Pose/PhysicsBones)
};

struct CatalogEntry
{
    const char* group; // panel tab ("Torso", "Left arm", ...)
    const char* label;
    std::vector<const char*> bones; // first bone is the primary one (used to read values back)
    EntryMode mode = EntryMode::Single;
    bool allowsTranslation = false;
    bool detail = false;      // close-up gizmo only, not listed in the panel (FingerSegment entries are always detail)
    bool markerAtMiddle = false; // gizmo drawn halfway along the bone (palm bones start right next to the wrist)
    // Physics skeletons (hair, clothes, breasts) of the selected character, listed after the body parts:
    const char* skeleton = nullptr;  // panel name of the skeleton ("Hair", "Breasts", component name)
    const char* chain = nullptr;     // panel name of the chain (the bones hanging from one body bone)
    const char* chainRoot = nullptr; // first bone of the chain (its physics switch is kept there)
    int32_t chainIndex = 0;          // 1-based place of the bone in its chain (0 for a group)
    bool ikMove = false;             // moving it bends the chain above it (chain IK) instead of offsetting it
};

// Body parts, then the physics bones of the selected character (indices of the body parts never change).
const std::vector<CatalogEntry>& GetBoneCatalog();
size_t GetBodyPartCount();
// Rebuilds the physics entries when the selected character's physics skeletons changed (game thread, never while a
// reference from GetBoneCatalog is in use). Returns the revision of the list.
uint32_t RefreshPhysicsEntries();
} // namespace APM::Ui
