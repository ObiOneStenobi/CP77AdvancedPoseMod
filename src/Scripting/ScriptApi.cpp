#include "Scripting/ScriptApi.hpp"

#include "Core/Log.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <string>
#include <vector>

// Global native functions exposed to redscript (and to CET through the RTTI).
// Declarations for redscript live in scripts/AdvancedPoseMod/Natives.reds.
namespace
{
using RED4ext::CName;
using RED4ext::CStackFrame;
using RED4ext::CString;
using RED4ext::IScriptable;

template<typename T>
void Return(void* aOut, const T& aValue)
{
    if (aOut)
    {
        *static_cast<T*>(aOut) = aValue;
    }
}

std::string ToString(CName aName)
{
    const char* text = aName.ToString();
    return text ? std::string(text) : std::string();
}

// Cached list of saved pose names, refreshed by APM_GetPoseCount().
std::vector<std::string> g_poseNames;

// --- Photo Mode / skeleton ---------------------------------------------------------------------------------------

void IsPhotoModeActive(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::PhotoModeTracker::IsActive());
}

void GetBoneCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, static_cast<int32_t>(APM::Pose::TargetRegistry::GetMainSkeletonBones().size()));
}

void GetBoneName(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;

    const auto bones = APM::Pose::TargetRegistry::GetMainSkeletonBones();
    Return(aOut, (index >= 0 && index < static_cast<int32_t>(bones.size())) ? CName(bones[index]) : CName());
}

// --- Bone overrides ----------------------------------------------------------------------------------------------

void SetBoneRotation(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName bone;
    float x = 0.f, y = 0.f, z = 0.f;
    RED4ext::GetParameter(aFrame, &bone);
    RED4ext::GetParameter(aFrame, &x);
    RED4ext::GetParameter(aFrame, &y);
    RED4ext::GetParameter(aFrame, &z);
    aFrame->code++;

    const auto name = ToString(bone);
    if (!name.empty())
    {
        APM::Pose::PoseOverrides::SetRotation(name, x, y, z);
    }
    Return(aOut, !name.empty());
}

void SetBoneTranslation(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName bone;
    float x = 0.f, y = 0.f, z = 0.f;
    RED4ext::GetParameter(aFrame, &bone);
    RED4ext::GetParameter(aFrame, &x);
    RED4ext::GetParameter(aFrame, &y);
    RED4ext::GetParameter(aFrame, &z);
    aFrame->code++;

    const auto name = ToString(bone);
    if (!name.empty())
    {
        APM::Pose::PoseOverrides::SetTranslation(name, x, y, z);
    }
    Return(aOut, !name.empty());
}

// Returns (x, y, z, w): xyz = value, w = 1 if the bone has this override, 0 otherwise.
void GetBoneRotation(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName bone;
    RED4ext::GetParameter(aFrame, &bone);
    aFrame->code++;

    std::array<float, 3> value{};
    const bool has = APM::Pose::PoseOverrides::GetRotation(ToString(bone), value);
    Return(aOut, RED4ext::Vector4{value[0], value[1], value[2], has ? 1.f : 0.f});
}

void GetBoneTranslation(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName bone;
    RED4ext::GetParameter(aFrame, &bone);
    aFrame->code++;

    std::array<float, 3> value{};
    const bool has = APM::Pose::PoseOverrides::GetTranslation(ToString(bone), value);
    Return(aOut, RED4ext::Vector4{value[0], value[1], value[2], has ? 1.f : 0.f});
}

void ResetBone(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    CName bone;
    RED4ext::GetParameter(aFrame, &bone);
    aFrame->code++;
    APM::Pose::PoseOverrides::ResetBone(ToString(bone));
}

void ResetAll(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    APM::Pose::PoseOverrides::ResetAll();
}

// --- IK chains: "LeftHand", "RightHand", "LeftFoot", "RightFoot" ------------------------------------------------

void SetIkChain(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName chainName;
    bool enabled = false;
    float x = 0.f, y = 0.f, z = 0.f, swivel = 0.f;
    RED4ext::GetParameter(aFrame, &chainName);
    RED4ext::GetParameter(aFrame, &enabled);
    RED4ext::GetParameter(aFrame, &x);
    RED4ext::GetParameter(aFrame, &y);
    RED4ext::GetParameter(aFrame, &z);
    RED4ext::GetParameter(aFrame, &swivel);
    aFrame->code++;

    APM::Pose::IkChain chain;
    if (!APM::Pose::ParseIkChain(ToString(chainName), chain))
    {
        Return(aOut, false);
        return;
    }
    APM::Pose::IkChainSettings settings;
    settings.enabled = enabled;
    settings.offset = {x, y, z};
    settings.swivel = swivel;
    APM::Pose::PoseOverrides::SetIkChain(chain, settings);
    Return(aOut, true);
}

// Returns (offsetX, offsetY, offsetZ, swivel). Use APM_IsIkChainEnabled for the switch.
void GetIkChain(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName chainName;
    RED4ext::GetParameter(aFrame, &chainName);
    aFrame->code++;

    APM::Pose::IkChain chain;
    APM::Pose::IkChainSettings settings;
    if (APM::Pose::ParseIkChain(ToString(chainName), chain))
    {
        settings = APM::Pose::PoseOverrides::GetIkChain(chain);
    }
    Return(aOut, RED4ext::Vector4{settings.offset[0], settings.offset[1], settings.offset[2], settings.swivel});
}

void IsIkChainEnabled(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CName chainName;
    RED4ext::GetParameter(aFrame, &chainName);
    aFrame->code++;

    APM::Pose::IkChain chain;
    Return(aOut, APM::Pose::ParseIkChain(ToString(chainName), chain) && APM::Pose::PoseOverrides::GetIkChain(chain).enabled);
}

void SetEnabled(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    bool enabled = false;
    RED4ext::GetParameter(aFrame, &enabled);
    aFrame->code++;
    APM::Pose::PoseOverrides::SetEnabled(enabled);
}

void IsEnabled(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::IsEnabled());
}

// --- Pose files --------------------------------------------------------------------------------------------------

void SavePose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::Save(name.c_str()));
}

void LoadPose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::Load(name.c_str()));
}

void GetPoseCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    g_poseNames = APM::Pose::PoseOverrides::ListSaved();
    Return(aOut, static_cast<int32_t>(g_poseNames.size()));
}

void GetPoseName(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;
    const bool valid = index >= 0 && index < static_cast<int32_t>(g_poseNames.size());
    Return(aOut, CString(valid ? g_poseNames[index].c_str() : ""));
}

// --- Registration ------------------------------------------------------------------------------------------------

struct Param
{
    const char* type;
    const char* name;
};

template<typename TFunc>
void Register(const char* aName, TFunc aFunc, const char* aReturnType, std::initializer_list<Param> aParams)
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* func = RED4ext::CGlobalFunction::Create(aName, aName, aFunc);
    RED4ext::CBaseFunction::Flags flags{};
    flags.isNative = true;
    flags.isStatic = true;
    func->flags = flags;
    for (const auto& param : aParams)
    {
        func->AddParam(param.type, param.name);
    }
    if (aReturnType)
    {
        func->SetReturnType(aReturnType);
    }
    rtti->RegisterFunction(func);
}
} // namespace

namespace APM::ScriptApi
{
void Register()
{
    ::Register("APM_IsPhotoModeActive", &IsPhotoModeActive, "Bool", {});
    ::Register("APM_GetBoneCount", &GetBoneCount, "Int32", {});
    ::Register("APM_GetBoneName", &GetBoneName, "CName", {{"Int32", "index"}});

    ::Register("APM_SetBoneRotation", &SetBoneRotation, "Bool",
               {{"CName", "bone"}, {"Float", "x"}, {"Float", "y"}, {"Float", "z"}});
    ::Register("APM_SetBoneTranslation", &SetBoneTranslation, "Bool",
               {{"CName", "bone"}, {"Float", "x"}, {"Float", "y"}, {"Float", "z"}});
    ::Register("APM_GetBoneRotation", &GetBoneRotation, "Vector4", {{"CName", "bone"}});
    ::Register("APM_GetBoneTranslation", &GetBoneTranslation, "Vector4", {{"CName", "bone"}});
    ::Register("APM_ResetBone", &ResetBone, nullptr, {{"CName", "bone"}});
    ::Register("APM_ResetAll", &ResetAll, nullptr, {});

    ::Register("APM_SetIkChain", &SetIkChain, "Bool",
               {{"CName", "chain"}, {"Bool", "enabled"}, {"Float", "x"}, {"Float", "y"}, {"Float", "z"}, {"Float", "swivel"}});
    ::Register("APM_GetIkChain", &GetIkChain, "Vector4", {{"CName", "chain"}});
    ::Register("APM_IsIkChainEnabled", &IsIkChainEnabled, "Bool", {{"CName", "chain"}});

    ::Register("APM_SetEnabled", &SetEnabled, nullptr, {{"Bool", "enabled"}});
    ::Register("APM_IsEnabled", &IsEnabled, "Bool", {});

    ::Register("APM_SavePose", &SavePose, "Bool", {{"String", "name"}});
    ::Register("APM_LoadPose", &LoadPose, "Bool", {{"String", "name"}});
    ::Register("APM_GetPoseCount", &GetPoseCount, "Int32", {});
    ::Register("APM_GetPoseName", &GetPoseName, "String", {{"Int32", "index"}});

    Log::Info("Script API registered");
}
} // namespace APM::ScriptApi
