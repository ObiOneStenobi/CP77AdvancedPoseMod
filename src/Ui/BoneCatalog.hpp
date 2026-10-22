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
    bool closeUp = false;        // listed in the panel, but its gizmo only shows close up (like the fingers)
};

const std::vector<CatalogEntry>& GetBoneCatalog();
} // namespace APM::Ui
