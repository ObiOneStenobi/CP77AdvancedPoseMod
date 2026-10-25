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
// has a value (0-1) per character, set by the user or else the Photo Mode expression's.
// - Through the game's facial solver (animAnimNode_Sermo, see Game/AnimGraphHook): the face_rig pose carries one float
//   track per face pose, which the Photo Mode expression sets; the values the user set replace those tracks before
//   the solver reads them, so the game applies its corrective poses and each head's own shapes.
// - Fallback, for faces the solver does not pose (hook not installed) and for the poses a face rig has no track for:
//   the poses read from the head facial setups by tools/face_poses.py into
//   red4ext/plugins/AdvancedPoseMod/face/<head>.json are added as local deltas of the face_rig bones (rotation * pose
//   rotation, translation + pose translation; between two inbetweens the rotation is slerped and the translation
//   lerped). Male characters (male V, added characters the game reports as male) use pma.json, the others pwa.json.
//   No correctives.
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

// Face rig, every frame, before the solver: the character's set values into its face tracks (aNames: the rig's track
// names); aApply false (pose off) only reads the Photo Mode expression. Afterwards Apply only adds the poses that rig
// has no track for.
void ApplyTracks(const Target& aTarget, float* aTracks, uint32_t aCount, const RED4ext::CName* aNames, uint32_t aNameCount,
                 const OverrideSnapshot& aPose, bool aApply);
// Face rig, every frame, after the graph: the set poses as bone deltas (fallback, see above).
void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose);

// The selected character's Photo Mode expression as the panel shows it: read from the tracks when the face is first
// seen and shortly after the expression changes, then kept (an animated expression does not make the sliders move).
// 0 when the solver did not pose the face. Can be above 1 (expressions of other mods).
float GameValue(size_t aPose);
// What the panel shows for a pose: the set value, else the Photo Mode one.
float ShownValue(size_t aPose);
// Set and different from the Photo Mode value.
bool IsEdited(size_t aPose);
// Photo Mode expression of a character chosen in the menu (aExpression: its index): when it changes, the face edits are reset (undo kept) and the shown
// expression is read again.
void OnExpressionChanged(uint32_t aCharacter, int32_t aExpression);
void ResetCharacter(uint32_t aCharacter);
void ResetSession();
// Male character (pma head for the bone fallback): male V, or an added character the game reports as male.
void SetCharacterMale(uint32_t aCharacter, bool aMale);

// Saved expressions (the whole face: the value of every pose, by name):
// red4ext/plugins/AdvancedPoseMod/expressions/<name>.json
std::filesystem::path GetDirectory();
std::vector<std::string> ListSaved();
bool Save(const std::string& aName);
std::string SaveNew();
bool Load(const std::string& aName);
bool Delete(const std::string& aName);
// Renames a saved expression; returns the new name (made unique), or "" on failure.
std::string Rename(const std::string& aOldName, const std::string& aNewName);
} // namespace APM::Pose::FacePoses
