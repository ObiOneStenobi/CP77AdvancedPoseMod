#pragma once

#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/CName.hpp>
#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Expression editor: the game's own face poses (FACS: "eye_l_blink", "lips_l_corner_up", "jaw_mid_open"...). Each pose
// has a weight (0-1) per character.
// - Through the game's facial solver (animAnimNode_Sermo, see Game/AnimGraphHook): the face_rig pose carries one float
//   track per face pose, which the Photo Mode expression sets; the weights are added to those tracks (or replace them)
//   before the solver reads them, so the game applies its corrective poses and each head's own shapes.
// - Fallback, for faces the solver does not pose (hook not installed): the poses read from the head facial setups by
//   tools/face_poses.py into red4ext/plugins/AdvancedPoseMod/face/<head>.json are added as local deltas of the face_rig
//   bones (rotation * pose rotation, translation + pose translation; between two inbetweens the rotation is slerped
//   and the translation lerped). Female V uses pwa.json, male V pma.json, added characters pwa.json. No correctives.
namespace APM::Pose::FacePoses
{
// Reads face/*.json (at plugin load, before the animation hooks run).
void LoadData();
bool IsAvailable();

// Poses, in the order of pwa.json (the same index in every head and in FaceWeights), then those that act only through
// the solver (their main poses are empty: lips_together_up / dn, lips_tighten_up / dn, jaw_mid_close / clench).
size_t Count();
const char* Name(size_t aPose);
const char* Zone(size_t aPose); // "brows", "eyelids", "nose", "cheeks", "lips", "jaw", "ears", "tongue"
const char* Side(size_t aPose); // "l", "r" or ""
int32_t Mirror(size_t aPose);   // the same pose on the other side, or -1
int32_t Find(const std::string& aName);

// Face rig, every frame, before the solver: the character's weights into its face tracks (aNames: the rig's track
// names); aApply false (pose off) only reads the Photo Mode expression. Afterwards Apply leaves that face alone.
void ApplyTracks(const Target& aTarget, float* aTracks, uint32_t aCount, const RED4ext::CName* aNames, uint32_t aNameCount,
                 const OverrideSnapshot& aPose, bool aApply);
// Face rig, every frame, after the graph (fallback when the solver did not take the weights): the weighted poses.
void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose);
// The selected character's weights become the Photo Mode expression it shows, replacing it from then on (the panel
// then edits it). False if its face was not seen through the solver.
bool CopyGameExpression();
// Weight a reset gives a pose back: 0, or while replacing, the Photo Mode expression's.
float ResetWeight(size_t aPose);

// Saved expressions (pose weights by name): red4ext/plugins/AdvancedPoseMod/expressions/<name>.json
std::filesystem::path GetDirectory();
std::vector<std::string> ListSaved();
bool Save(const std::string& aName);
std::string SaveNew();
bool Load(const std::string& aName);
bool Delete(const std::string& aName);
// Renames a saved expression; returns the new name (made unique), or "" on failure.
std::string Rename(const std::string& aOldName, const std::string& aNewName);
} // namespace APM::Pose::FacePoses
