#pragma once

#include "Math/Quat.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Per-bone overrides applied on top of the animated pose ("additive" mode):
//   final rotation    = animated rotation * override rotation   (bone local space)
//   final translation = animated translation + override offset  (parent space, meters)
//
// Written from the game thread (scripts / UI), read from animation worker threads through immutable snapshots.
namespace APM::Pose
{
struct BoneOverride
{
    uint64_t boneHash = 0;
    std::string boneName;

    bool hasRotation = false;
    std::array<float, 3> rotationEuler{}; // degrees, applied Z * Y * X around local axes
    Math::Quat rotation{};

    bool hasTranslation = false;
    std::array<float, 3> translation{}; // meters
};

struct OverrideSnapshot
{
    std::vector<BoneOverride> bones;
};

namespace PoseOverrides
{
// Master switch. Editing a bone or loading a pose enables it; leaving Photo Mode disables it
// (the edited pose stays in memory so the player can turn it back on).
void SetEnabled(bool aEnabled);
bool IsEnabled();

// Snapshot used by the animation hook. Never null.
std::shared_ptr<const OverrideSnapshot> GetSnapshot();

void SetRotation(const std::string& aBone, float aX, float aY, float aZ);
void SetTranslation(const std::string& aBone, float aX, float aY, float aZ);
bool GetRotation(const std::string& aBone, std::array<float, 3>& aOut);
bool GetTranslation(const std::string& aBone, std::array<float, 3>& aOut);
void ResetBone(const std::string& aBone);
void ResetAll();
size_t Count();

// Pose files live in red4ext/plugins/AdvancedPoseMod/poses/<name>.json
std::filesystem::path GetPoseDirectory();
bool Save(const std::string& aName);
bool Load(const std::string& aName);
std::vector<std::string> ListSaved();
} // namespace PoseOverrides
} // namespace APM::Pose
