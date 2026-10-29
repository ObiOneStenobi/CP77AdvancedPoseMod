#include "Pose/PoseOverrides.hpp"

#include "Core/Log.hpp"
#include "Core/NamedFiles.hpp"
#include "Core/Runtime.hpp"
#include "Pose/Characters.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/CName.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <tuple>
#include <unordered_set>

namespace
{
constexpr const char* PoseFormat = "AdvancedPoseMod.Pose";
constexpr int PoseFormatVersion = 2; // 2: "base" holds the whole pose

std::mutex g_mutex; // guards the edit state of every character and the replacement of their snapshots

// Undo / redo history (guarded by g_mutex).
struct EditState
{
    std::vector<APM::Pose::BoneOverride> bones;
    std::array<APM::Pose::IkChainSettings, APM::Pose::IkChainCount> ik{};
    std::shared_ptr<const APM::Pose::BasePose> base;
    APM::Pose::FaceWeights faceWeights = APM::Pose::UnsetFaceWeights();
    std::string faceName;
    uint64_t propRight = 0;
    uint64_t propLeft = 0;
};
constexpr size_t MaxHistory = 100;

// Everything one character's pose is made of (see Pose/Characters.hpp).
struct CharacterState
{
    std::atomic<bool> enabled{false};
    std::vector<APM::Pose::BoneOverride> bones;
    std::array<APM::Pose::IkChainSettings, APM::Pose::IkChainCount> ik{};
    std::shared_ptr<const APM::Pose::BasePose> base;
    APM::Pose::FaceWeights faceWeights = APM::Pose::UnsetFaceWeights();
    std::string faceName; // saved expression loaded last (shown by the panel)
    std::vector<EditState> undo;
    std::vector<EditState> redo;
    std::atomic<uint64_t> propRight{0};
    std::atomic<uint64_t> propLeft{0};
    // Read by the animation threads (guarded by g_snapshotMutex, held for a pointer copy only).
    std::shared_ptr<const APM::Pose::OverrideSnapshot> snapshot = std::make_shared<APM::Pose::OverrideSnapshot>();
};
std::array<CharacterState, APM::Pose::Characters::Count> g_states;
std::mutex g_snapshotMutex;

// The selected character's state (the panel and the scripts edit it).
CharacterState& S()
{
    return g_states[APM::Pose::Characters::Selected()];
}

uint64_t HashBone(const std::string& aBone)
{
    return RED4ext::CName(aBone.c_str()).hash;
}

// Must be called with g_mutex held.
void PublishLocked(CharacterState& aState)
{
    auto snapshot = std::make_shared<APM::Pose::OverrideSnapshot>();
    snapshot->ik = aState.ik;
    snapshot->base = aState.base;
    snapshot->faceWeights = aState.faceWeights;
    for (const auto& bone : aState.bones)
    {
        if (bone.hasRotation || bone.hasTranslation)
        {
            snapshot->bones.push_back(bone);
        }
    }
    std::scoped_lock lock(g_snapshotMutex);
    aState.snapshot = std::move(snapshot);
}

void PublishLocked()
{
    PublishLocked(S());
}

// Must be called with g_mutex held.
void ClearLocked(CharacterState& aState)
{
    aState.bones.clear();
    aState.ik = {};
    aState.base.reset();
    aState.faceWeights = APM::Pose::UnsetFaceWeights();
    aState.faceName.clear();
    aState.undo.clear();
    aState.redo.clear();
    aState.propRight = 0;
    aState.propLeft = 0;
    PublishLocked(aState);
    aState.enabled = false;
}

bool SameBones(const std::vector<APM::Pose::BoneOverride>& a, const std::vector<APM::Pose::BoneOverride>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.boneHash != y.boneHash || x.hasRotation != y.hasRotation || x.hasTranslation != y.hasTranslation ||
            x.rotation.i != y.rotation.i || x.rotation.j != y.rotation.j || x.rotation.k != y.rotation.k ||
            x.rotation.r != y.rotation.r || x.translation != y.translation || x.uiAngles != y.uiAngles)
        {
            return false;
        }
    }
    return true;
}

bool SameIk(const std::array<APM::Pose::IkChainSettings, APM::Pose::IkChainCount>& a,
            const std::array<APM::Pose::IkChainSettings, APM::Pose::IkChainCount>& b)
{
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].enabled != b[i].enabled || a[i].followBody != b[i].followBody || a[i].offset != b[i].offset ||
            a[i].swivel != b[i].swivel)
        {
            return false;
        }
    }
    return true;
}

// Must be called with g_mutex held.
EditState CurrentStateLocked(const CharacterState& aState)
{
    return {aState.bones, aState.ik, aState.base, aState.faceWeights, aState.faceName, aState.propRight.load(),
            aState.propLeft.load()};
}

EditState CurrentStateLocked()
{
    return CurrentStateLocked(S());
}

bool SameAsCurrentLocked(const EditState& aState, const CharacterState& aCurrent)
{
    return aState.base == aCurrent.base && aState.faceWeights == aCurrent.faceWeights &&
           aState.faceName == aCurrent.faceName && aState.propRight == aCurrent.propRight &&
           aState.propLeft == aCurrent.propLeft && SameIk(aState.ik, aCurrent.ik) && SameBones(aState.bones, aCurrent.bones);
}

bool SameAsCurrentLocked(const EditState& aState)
{
    return SameAsCurrentLocked(aState, S());
}

void RestoreLocked(EditState&& aState)
{
    S().bones = std::move(aState.bones);
    S().ik = aState.ik;
    S().base = std::move(aState.base);
    S().faceWeights = aState.faceWeights;
    S().faceName = std::move(aState.faceName);
    S().propRight = aState.propRight;
    S().propLeft = aState.propLeft;
}

// Records the current state before a change, unless nothing changed since the last record.
void CheckpointLocked(CharacterState& aState)
{
    if (!aState.undo.empty() && SameAsCurrentLocked(aState.undo.back(), aState))
    {
        return;
    }
    aState.undo.push_back(CurrentStateLocked(aState));
    if (aState.undo.size() > MaxHistory)
    {
        aState.undo.erase(aState.undo.begin());
    }
    aState.redo.clear();
}

void CheckpointLocked()
{
    CheckpointLocked(S());
}

// Must be called with g_mutex held.
APM::Pose::BoneOverride& FindOrAddLocked(const std::string& aBone)
{
    const uint64_t hash = HashBone(aBone);
    auto it = std::find_if(S().bones.begin(), S().bones.end(), [hash](const auto& b) { return b.boneHash == hash; });
    if (it != S().bones.end())
    {
        return *it;
    }

    APM::Pose::BoneOverride bone;
    bone.boneHash = hash;
    bone.boneName = aBone;
    S().bones.push_back(bone);
    return S().bones.back();
}

const APM::Pose::BoneOverride* FindLocked(const std::string& aBone)
{
    const uint64_t hash = HashBone(aBone);
    auto it = std::find_if(S().bones.begin(), S().bones.end(), [hash](const auto& b) { return b.boneHash == hash; });
    return it != S().bones.end() ? &*it : nullptr;
}

bool IsZero(const std::array<float, 3>& aValues)
{
    return aValues[0] == 0.f && aValues[1] == 0.f && aValues[2] == 0.f;
}

// Keeps file names portable and prevents path traversal.
std::string SanitizeName(const std::string& aName)
{
    std::string out;
    for (const char ch : aName)
    {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == '_' || ch == '-' || ch == ' ';
        out.push_back(ok ? ch : '_');
        if (out.size() >= 64)
        {
            break;
        }
    }
    while (!out.empty() && out.back() == ' ')
    {
        out.pop_back();
    }
    return out;
}
} // namespace

namespace APM::Pose
{
namespace
{
constexpr const char* IkChainNames[IkChainCount] = {"LeftHand", "RightHand", "LeftFoot", "RightFoot"};
}

bool ParseIkChain(const std::string& aName, IkChain& aOut)
{
    for (size_t i = 0; i < IkChainCount; ++i)
    {
        if (aName == IkChainNames[i])
        {
            aOut = static_cast<IkChain>(i);
            return true;
        }
    }
    return false;
}

const char* GetIkChainName(IkChain aChain)
{
    const auto index = static_cast<size_t>(aChain);
    return index < IkChainCount ? IkChainNames[index] : "";
}
} // namespace APM::Pose

namespace APM::Pose::PoseOverrides
{
void SetIkChain(IkChain aChain, const IkChainSettings& aSettings)
{
    const auto index = static_cast<size_t>(aChain);
    if (index >= IkChainCount)
    {
        return;
    }
    std::scoped_lock lock(g_mutex);
    S().ik[index] = aSettings;
    PublishLocked();
    if (aSettings.enabled)
    {
        S().enabled = true;
    }
}

IkChainSettings GetIkChain(IkChain aChain)
{
    const auto index = static_cast<size_t>(aChain);
    std::scoped_lock lock(g_mutex);
    return index < IkChainCount ? S().ik[index] : IkChainSettings{};
}

void SetEnabled(bool aEnabled)
{
    if (S().enabled.exchange(aEnabled) != aEnabled)
    {
        Log::Info("Custom pose %s", aEnabled ? "enabled" : "disabled");
    }
}

bool IsEnabled()
{
    return S().enabled.load(std::memory_order_relaxed);
}

std::shared_ptr<const OverrideSnapshot> GetSnapshot()
{
    std::scoped_lock lock(g_snapshotMutex);
    return S().snapshot;
}

void SetRotation(const std::string& aBone, float aX, float aY, float aZ)
{
    std::scoped_lock lock(g_mutex);
    auto& bone = FindOrAddLocked(aBone);
    bone.rotationEuler = {aX, aY, aZ};
    bone.rotation = Math::FromEulerXYZDegrees(aX, aY, aZ);
    bone.hasUiAngles = false;
    bone.hasRotation = !IsZero(bone.rotationEuler);
    PublishLocked();
    S().enabled = true;
}

void SetTranslation(const std::string& aBone, float aX, float aY, float aZ)
{
    std::scoped_lock lock(g_mutex);
    auto& bone = FindOrAddLocked(aBone);
    bone.translation = {aX, aY, aZ};
    bone.hasTranslation = !IsZero(bone.translation);
    PublishLocked();
    S().enabled = true;
}

void SetRotationQuat(const std::string& aBone, const Math::Quat& aRotation, const std::array<float, 3>& aUiAngles)
{
    std::scoped_lock lock(g_mutex);
    auto& bone = FindOrAddLocked(aBone);
    bone.rotation = Math::Normalize(aRotation);
    bone.rotationEuler = {};
    bone.uiAngles = aUiAngles;
    bone.hasUiAngles = true;
    bone.hasRotation = !IsZero(aUiAngles);
    PublishLocked();
    S().enabled = true;
}

bool GetUiAngles(const std::string& aBone, std::array<float, 3>& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto* bone = FindLocked(aBone);
    const bool has = bone && bone->hasRotation && bone->hasUiAngles;
    aOut = has ? bone->uiAngles : std::array<float, 3>{};
    return has;
}

bool GetRotation(const std::string& aBone, std::array<float, 3>& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto* bone = FindLocked(aBone);
    aOut = (bone && bone->hasRotation) ? bone->rotationEuler : std::array<float, 3>{};
    return bone && bone->hasRotation;
}

bool GetTranslation(const std::string& aBone, std::array<float, 3>& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto* bone = FindLocked(aBone);
    aOut = (bone && bone->hasTranslation) ? bone->translation : std::array<float, 3>{};
    return bone && bone->hasTranslation;
}

void ResetBone(const std::string& aBone)
{
    std::scoped_lock lock(g_mutex);
    const uint64_t hash = HashBone(aBone);
    std::erase_if(S().bones, [hash](const auto& b) { return b.boneHash == hash; });
    PublishLocked();
}

void ResetAll(bool aKeepFace)
{
    std::scoped_lock lock(g_mutex);
    CheckpointLocked();
    S().bones.clear();
    S().ik = {};
    S().base.reset();
    S().propRight = 0; // the hands hold the items of the Photo Mode pose again
    S().propLeft = 0;
    if (!aKeepFace)
    {
        S().faceWeights = UnsetFaceWeights();
        S().faceName.clear();
    }
    PublishLocked();
}

void ClearSession()
{
    std::scoped_lock lock(g_mutex);
    for (auto& state : g_states)
    {
        ClearLocked(state);
    }
}

void SelectCharacter(uint32_t aCharacter)
{
    std::scoped_lock lock(g_mutex); // never in the middle of an edit of the previously selected character
    Characters::Select(aCharacter);
}

void ClearCharacter(uint32_t aCharacter)
{
    if (aCharacter >= Characters::Count)
    {
        return;
    }
    std::scoped_lock lock(g_mutex);
    ClearLocked(g_states[aCharacter]);
}

bool HasEdits(uint32_t aCharacter)
{
    if (aCharacter >= Characters::Count)
    {
        return false;
    }
    const auto& state = g_states[aCharacter];
    std::shared_ptr<const OverrideSnapshot> snapshot;
    {
        std::scoped_lock lock(g_snapshotMutex);
        snapshot = state.snapshot;
    }
    return !snapshot->IsEmpty() || state.propRight != 0 || state.propLeft != 0;
}

bool IsEnabled(uint32_t aCharacter)
{
    return aCharacter < Characters::Count && g_states[aCharacter].enabled.load(std::memory_order_relaxed);
}

std::shared_ptr<const OverrideSnapshot> GetSnapshot(uint32_t aCharacter)
{
    std::scoped_lock lock(g_snapshotMutex);
    return g_states[aCharacter < Characters::Count ? aCharacter : 0].snapshot;
}

size_t Count()
{
    const auto snapshot = GetSnapshot();
    size_t count = snapshot->bones.size();
    for (const auto& chain : snapshot->ik)
    {
        count += chain.enabled ? 1 : 0;
    }
    return count;
}

std::filesystem::path GetPoseDirectory()
{
    return GetRuntime().pluginDir / L"poses";
}

bool Save(const std::string& aName)
{
    // A pose in a subfolder is saved in it ("Some mod/Sitting").
    const auto name = NamedFiles::SanitizeInFolder(GetPoseDirectory(), aName);
    if (name.empty())
    {
        Log::Warn("SavePose: invalid name");
        return false;
    }

    nlohmann::json root;
    root["format"] = PoseFormat;
    root["version"] = PoseFormatVersion;
    root["name"] = name;
    root["skeleton"] = "V";

    // The whole visible pose: body bones as local rotations (edits and IK included). Bones of the face stay
    // edits on top of the animation (the face keeps its Photo Mode expression).
    std::vector<GizmoState::CapturedBone> captured;
    const bool full = GizmoState::CaptureLocalPose(captured);
    std::unordered_set<uint64_t> baseBones;
    if (full)
    {
        root["mode"] = "full";
        auto& base = root["base"] = nlohmann::json::object();
        for (const auto& bone : captured)
        {
            if (!bone.inBase)
            {
                continue;
            }
            nlohmann::json entry;
            entry["r"] = {bone.rotation.i, bone.rotation.j, bone.rotation.k, bone.rotation.r};
            if (bone.keepTranslation)
            {
                entry["t"] = {bone.translation.x, bone.translation.y, bone.translation.z};
            }
            base[bone.name] = std::move(entry);
            baseBones.insert(bone.hash);
        }
    }
    else
    {
        root["mode"] = "additive";
    }

    auto& bones = root["bones"] = nlohmann::json::array();
    for (const auto& bone : GetSnapshot()->bones)
    {
        if (baseBones.contains(bone.boneHash))
        {
            continue; // already part of the saved pose
        }
        nlohmann::json entry;
        entry["name"] = bone.boneName;
        if (bone.hasRotation)
        {
            if (bone.hasUiAngles)
            {
                entry["rotationQuat"] = {bone.rotation.i, bone.rotation.j, bone.rotation.k, bone.rotation.r};
                entry["ui"] = bone.uiAngles;
            }
            else
            {
                entry["rotation"] = bone.rotationEuler;
            }
        }
        if (bone.hasTranslation)
        {
            entry["translation"] = bone.translation;
        }
        bones.push_back(std::move(entry));
    }

    auto& ik = root["ik"] = nlohmann::json::object();
    const auto snapshot = GetSnapshot();
    for (size_t i = 0; i < IkChainCount && !full; ++i) // a full pose already has the IK result in it
    {
        const auto& chain = snapshot->ik[i];
        if (chain.enabled)
        {
            ik[GetIkChainName(static_cast<IkChain>(i))] = {
                {"offset", chain.offset}, {"swivel", chain.swivel}, {"pin", chain.followBody ? "body" : "world"}};
        }
    }

    if (S().propRight != 0 || S().propLeft != 0)
    {
        auto& props = root["props"] = nlohmann::json::object();
        // 1 = the hand was emptied on purpose (the Photo Mode pose's own item removed)
        const auto text = [](uint64_t aRecord) { return aRecord == 1 ? std::string("empty") : std::to_string(aRecord); };
        if (S().propRight != 0)
        {
            props["right"] = text(S().propRight.load());
        }
        if (S().propLeft != 0)
        {
            props["left"] = text(S().propLeft.load());
        }
    }

    std::filesystem::path path;
    NamedFiles::Resolve(GetPoseDirectory(), name, path);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        Log::Error("SavePose: cannot write %s", path.string().c_str());
        return false;
    }
    out << root.dump(2);
    Log::Info("Pose saved: %s (%s, %zu body bones, %zu edits, %zu IK chains)", name.c_str(), full ? "full" : "edits only",
              baseBones.size(), bones.size(), ik.size());
    return true;
}

bool Load(const std::string& aName)
{
    const std::string& name = aName;
    std::filesystem::path path;
    if (!NamedFiles::Resolve(GetPoseDirectory(), name, path))
    {
        Log::Warn("LoadPose: invalid name: %s", name.c_str());
        return false;
    }
    std::ifstream in(path);
    if (!in)
    {
        Log::Warn("LoadPose: file not found: %s", NamedFiles::ToUtf8(path).c_str());
        return false;
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(in);
    }
    catch (const std::exception& e)
    {
        Log::Error("LoadPose: invalid JSON in %s: %s", name.c_str(), e.what());
        return false;
    }

    if (root.value("format", "") != PoseFormat)
    {
        Log::Error("LoadPose: %s is not an AdvancedPoseMod pose file", name.c_str());
        return false;
    }

    std::vector<BoneOverride> loaded;
    std::array<IkChainSettings, IkChainCount> loadedIk{};
    std::shared_ptr<BasePose> loadedBase;
    try
    {
        if (root.contains("base"))
        {
            loadedBase = std::make_shared<BasePose>();
            loadedBase->name = name;
            for (const auto& [boneName, entry] : root["base"].items())
            {
                BasePoseBone bone;
                bone.boneHash = HashBone(boneName);
                const auto q = entry.at("r").get<std::array<float, 4>>();
                bone.rotation = Math::Normalize(Math::Quat{q[0], q[1], q[2], q[3]});
                if (entry.contains("t"))
                {
                    bone.translation = entry["t"].get<std::array<float, 3>>();
                    bone.hasTranslation = true;
                }
                loadedBase->bones.push_back(bone);
            }
        }

        if (root.contains("ik"))
        {
            for (const auto& [chainName, entry] : root["ik"].items())
            {
                IkChain chain;
                if (!ParseIkChain(chainName, chain))
                {
                    continue;
                }
                auto& settings = loadedIk[static_cast<size_t>(chain)];
                settings.enabled = true;
                settings.offset = entry.value("offset", std::array<float, 3>{});
                settings.swivel = entry.value("swivel", 0.f);
                settings.followBody = entry.value("pin", std::string("world")) == "body";
            }
        }

        for (const auto& entry : root.at("bones"))
        {
            BoneOverride bone;
            bone.boneName = entry.at("name").get<std::string>();
            bone.boneHash = HashBone(bone.boneName);
            if (entry.contains("rotationQuat"))
            {
                const auto q = entry["rotationQuat"].get<std::array<float, 4>>();
                bone.rotation = Math::Normalize(Math::Quat{q[0], q[1], q[2], q[3]});
                bone.uiAngles = entry.value("ui", std::array<float, 3>{});
                bone.hasUiAngles = entry.contains("ui");
                bone.hasRotation = true;
            }
            else if (entry.contains("rotation"))
            {
                bone.rotationEuler = entry["rotation"].get<std::array<float, 3>>();
                bone.rotation = Math::FromEulerXYZDegrees(bone.rotationEuler[0], bone.rotationEuler[1],
                                                         bone.rotationEuler[2]);
                bone.hasRotation = !IsZero(bone.rotationEuler);
            }
            if (entry.contains("translation"))
            {
                bone.translation = entry["translation"].get<std::array<float, 3>>();
                bone.hasTranslation = !IsZero(bone.translation);
            }
            loaded.push_back(std::move(bone));
        }
    }
    catch (const std::exception& e)
    {
        Log::Error("LoadPose: malformed bone entry in %s: %s", name.c_str(), e.what());
        return false;
    }

    {
        std::scoped_lock lock(g_mutex);
        CheckpointLocked();
        S().bones = std::move(loaded);
        S().ik = loadedIk;
        S().base = std::move(loadedBase);
        PublishLocked();
        const auto prop = [&](const char* aKey) -> uint64_t
        {
            if (!root.contains("props") || !root["props"].contains(aKey))
            {
                return 0;
            }
            try
            {
                const auto value = root["props"][aKey].get<std::string>();
                return value == "empty" ? 1 : std::stoull(value);
            }
            catch (...)
            {
                return 0;
            }
        };
        S().propRight = prop("right");
        S().propLeft = prop("left");
    }
    S().enabled = true;
    Log::Info("Pose loaded: %s (%zu edits, %zu base pose bones)", name.c_str(), Count(),
              GetSnapshot()->base ? GetSnapshot()->base->bones.size() : size_t(0));
    return true;
}

void SetFaceWeight(size_t aPose, float aWeight)
{
    if (aPose >= MaxFacePoses)
    {
        return;
    }
    std::scoped_lock lock(g_mutex);
    // Up to 4 (400%): the panel limits it to 1 unless the face limits are unlocked.
    S().faceWeights[aPose] = aWeight < 0.f ? FaceUnset : std::min(aWeight, 4.f);
    PublishLocked();
    S().enabled = true;
}

float GetFaceWeight(size_t aPose)
{
    std::scoped_lock lock(g_mutex);
    return aPose < MaxFacePoses ? S().faceWeights[aPose] : FaceUnset;
}

FaceWeights GetFaceWeights()
{
    std::scoped_lock lock(g_mutex);
    return S().faceWeights;
}

void SetFaceWeights(const FaceWeights& aWeights, const std::string& aName)
{
    std::scoped_lock lock(g_mutex);
    CheckpointLocked();
    S().faceWeights = aWeights;
    S().faceName = aName;
    PublishLocked();
    S().enabled = true;
}

std::string GetFaceName()
{
    std::scoped_lock lock(g_mutex);
    return S().faceName;
}

void ResetFace()
{
    std::scoped_lock lock(g_mutex);
    CheckpointLocked();
    S().faceWeights = UnsetFaceWeights();
    S().faceName.clear();
    PublishLocked();
}

bool ResetFaceOf(uint32_t aCharacter, const FaceWeights& aShown)
{
    if (aCharacter >= Characters::Count)
    {
        return false;
    }
    std::scoped_lock lock(g_mutex);
    auto& state = g_states[aCharacter];
    if (!AnyFaceWeight(state.faceWeights) && state.faceName.empty())
    {
        return false;
    }
    for (size_t p = 0; p < MaxFacePoses; ++p)
    {
        if (state.faceWeights[p] < 0.f)
        {
            state.faceWeights[p] = aShown[p];
        }
    }
    CheckpointLocked(state);
    state.faceWeights = UnsetFaceWeights();
    state.faceName.clear();
    PublishLocked(state);
    return true;
}

void SetHandProp(bool aRightHand, uint64_t aRecord)
{
    (aRightHand ? S().propRight : S().propLeft) = aRecord;
}

uint64_t GetHandProp(bool aRightHand)
{
    return (aRightHand ? S().propRight : S().propLeft).load();
}

void Checkpoint()
{
    std::scoped_lock lock(g_mutex);
    CheckpointLocked();
}

bool Undo()
{
    std::scoped_lock lock(g_mutex);
    // Skip records equal to the current state (an edit that started but changed nothing).
    while (!S().undo.empty() && SameAsCurrentLocked(S().undo.back()))
    {
        S().undo.pop_back();
    }
    if (S().undo.empty())
    {
        return false;
    }
    S().redo.push_back(CurrentStateLocked());
    RestoreLocked(std::move(S().undo.back()));
    S().undo.pop_back();
    PublishLocked();
    S().enabled = true;
    return true;
}

bool Redo()
{
    std::scoped_lock lock(g_mutex);
    if (S().redo.empty())
    {
        return false;
    }
    S().undo.push_back(CurrentStateLocked());
    RestoreLocked(std::move(S().redo.back()));
    S().redo.pop_back();
    PublishLocked();
    S().enabled = true;
    return true;
}

bool CanUndo()
{
    std::scoped_lock lock(g_mutex);
    for (auto it = S().undo.rbegin(); it != S().undo.rend(); ++it)
    {
        if (!SameAsCurrentLocked(*it))
        {
            return true;
        }
    }
    return false;
}

bool CanRedo()
{
    std::scoped_lock lock(g_mutex);
    return !S().redo.empty();
}

bool SetNeutralPose(bool aArmsDown)
{
    TargetRegistry::ReferencePose reference;
    if (!TargetRegistry::GetReferencePose(reference))
    {
        return false;
    }
    const size_t count = reference.boneNames.size();
    const uint64_t head = HashBone("Head");
    const uint64_t hips = HashBone("Hips");

    std::vector<Math::Quat> local = reference.localRotations;
    if (aArmsDown)
    {
        // A pose: upper arms turned down by 45 degrees (the reference pose holds them level).
        std::vector<Math::Quat> model(count);
        std::vector<Math::Vec3> position(count);
        for (size_t i = 0; i < count; ++i)
        {
            const int16_t parent = reference.parents[i];
            if (parent >= 0 && static_cast<size_t>(parent) < i)
            {
                model[i] = Math::Mul(model[parent], local[i]);
                position[i] = position[parent] + Math::Rotate(model[parent], reference.localTranslations[i]);
            }
            else
            {
                model[i] = local[i];
                position[i] = reference.localTranslations[i];
            }
        }
        for (const auto& [arm, foreArm, side] :
             {std::tuple{"LeftArm", "LeftForeArm", -1.f}, std::tuple{"RightArm", "RightForeArm", 1.f}})
        {
            const auto a = std::find(reference.boneNames.begin(), reference.boneNames.end(), HashBone(arm));
            const auto b = std::find(reference.boneNames.begin(), reference.boneNames.end(), HashBone(foreArm));
            if (a == reference.boneNames.end() || b == reference.boneNames.end())
            {
                continue;
            }
            const auto ia = static_cast<size_t>(std::distance(reference.boneNames.begin(), a));
            const auto ib = static_cast<size_t>(std::distance(reference.boneNames.begin(), b));
            const int16_t parent = reference.parents[ia];
            if (parent < 0 || static_cast<size_t>(parent) >= ia)
            {
                continue;
            }
            const Math::Vec3 current = Math::Normalize(position[ib] - position[ia]);
            const Math::Vec3 wanted = Math::Normalize(Math::Vec3{side, 0.f, -1.f});
            const Math::Vec3 axis = Math::Cross(current, wanted);
            if (Math::Length(axis) < 1e-5f)
            {
                continue;
            }
            const Math::Quat turn = Math::FromAxisAngle(axis, Math::AngleBetween(current, wanted));
            const Math::Quat armModel = Math::Mul(turn, model[ia]);
            local[ia] = Math::Normalize(Math::Mul(Math::Conjugate(model[parent]), armModel));
        }
    }

    auto base = std::make_shared<BasePose>();
    base->name = aArmsDown ? "A pose" : "T pose";
    std::vector<bool> inFace(count, false);
    for (size_t i = 0; i < count; ++i)
    {
        const int16_t parent = reference.parents[i];
        if (parent < 0 || static_cast<size_t>(parent) >= i)
        {
            continue; // root
        }
        inFace[i] = inFace[parent] || reference.boneNames[parent] == head;
        if (inFace[i])
        {
            continue;
        }
        BasePoseBone bone;
        bone.boneHash = reference.boneNames[i];
        bone.rotation = local[i];
        if (bone.boneHash == hips)
        {
            const auto& t = reference.localTranslations[i];
            bone.translation = {t.x, t.y, t.z};
            bone.hasTranslation = true;
        }
        base->bones.push_back(bone);
    }

    {
        std::scoped_lock lock(g_mutex);
        CheckpointLocked();
        // Keep only the face edits (eyes): everything else starts from the neutral pose.
        std::erase_if(S().bones, [](const BoneOverride& aBone) { return aBone.boneName.find("Eye") == std::string::npos; });
        S().ik = {};
        S().base = std::move(base);
        PublishLocked();
    }
    S().enabled = true;
    Log::Info("Neutral pose: %s", aArmsDown ? "A" : "T");
    return true;
}

std::string GetBasePoseName()
{
    std::scoped_lock lock(g_mutex);
    return S().base ? S().base->name : std::string();
}

std::vector<std::string> ListSaved()
{
    return NamedFiles::List(GetPoseDirectory());
}

bool Delete(const std::string& aName)
{
    const std::string& name = aName;
    std::filesystem::path path;
    if (!NamedFiles::Resolve(GetPoseDirectory(), name, path))
    {
        return false;
    }
    std::error_code ec;
    const bool removed = std::filesystem::remove(path, ec);
    Log::Info("Pose '%s' %s", name.c_str(), removed ? "deleted" : "not found");
    return removed;
}

std::string Rename(const std::string& aOldName, const std::string& aNewName)
{
    // The file stays in its folder (poses of other mods keep their subfolder); only its own name changes.
    const std::string& from = aOldName;
    const std::string folder = NamedFiles::FolderOf(from);
    // The field shows the whole name: a folder typed in front is the file's own one, not part of the new name.
    const bool typedFolder = !folder.empty() && aNewName.rfind(folder, 0) == 0;
    std::string base = SanitizeName(typedFolder ? aNewName.substr(folder.size()) : aNewName);
    while (!base.empty() && base.front() == ' ')
    {
        base.erase(base.begin());
    }
    std::filesystem::path source;
    if (base.empty() || !NamedFiles::Resolve(GetPoseDirectory(), from, source))
    {
        return {};
    }
    if (folder + base == from)
    {
        return from;
    }
    std::error_code ec;
    if (!std::filesystem::exists(source, ec))
    {
        return {};
    }
    std::string name = folder + base;
    std::filesystem::path target;
    for (int number = 2; NamedFiles::Resolve(GetPoseDirectory(), name, target) && std::filesystem::exists(target, ec);
         ++number)
    {
        // Same name in another case on a case-insensitive disk: renaming onto itself is fine.
        if (std::filesystem::equivalent(source, target, ec))
        {
            break;
        }
        if (number > 999)
        {
            return {};
        }
        name = folder + base + " " + std::to_string(number);
    }
    if (!NamedFiles::Resolve(GetPoseDirectory(), name, target))
    {
        return {};
    }
    std::filesystem::rename(source, target, ec);
    if (ec)
    {
        Log::Warn("Pose '%s' could not be renamed to '%s': %s", from.c_str(), name.c_str(), ec.message().c_str());
        return {};
    }
    Log::Info("Pose '%s' renamed to '%s'", from.c_str(), name.c_str());
    return name;
}

std::string SaveNew(const std::string& aFolder)
{
    const auto name = NamedFiles::FreeName(GetPoseDirectory(), aFolder, "Pose");
    return !name.empty() && Save(name) ? name : std::string();
}
} // namespace APM::Pose::PoseOverrides
