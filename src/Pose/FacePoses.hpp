#pragma once

#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Expression editor: the game's own face poses (FACS: "eye_l_blink", "lips_l_corner_up", "jaw_mid_open"...), read from
// the head facial setups by tools/face_poses.py into red4ext/plugins/AdvancedPoseMod/face/<head>.json. Each pose has a
// weight (0-1) per character; the weighted poses are added to the face the Photo Mode expression shows, as local
// deltas of the face_rig bones (rotation * pose rotation, translation + pose translation; between two inbetweens the
// rotation is slerped and the translation lerped). Female V uses pwa.json, male V pma.json, added characters pwa.json.
// The game's corrective poses (applied when several poses act together) are not included.
namespace APM::Pose::FacePoses
{
// Reads face/*.json (at plugin load, before the animation hooks run).
void LoadData();
bool IsAvailable();

// Poses, in the order of pwa.json (the same index in every head and in FaceWeights).
size_t Count();
const char* Name(size_t aPose);
const char* Zone(size_t aPose); // "brows", "eyelids", "nose", "cheeks", "lips", "jaw", "ears", "tongue"
const char* Side(size_t aPose); // "l", "r" or ""
int32_t Mirror(size_t aPose);   // the same pose on the other side, or -1
int32_t Find(const std::string& aName);

// Face rig, every frame: the character's weighted poses.
void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose);

// Saved expressions (pose weights by name): red4ext/plugins/AdvancedPoseMod/expressions/<name>.json
std::filesystem::path GetDirectory();
std::vector<std::string> ListSaved();
bool Save(const std::string& aName);
std::string SaveNew();
bool Load(const std::string& aName);
bool Delete(const std::string& aName);
} // namespace APM::Pose::FacePoses
