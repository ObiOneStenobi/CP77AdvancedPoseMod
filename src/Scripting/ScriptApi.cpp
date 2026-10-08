#include "Scripting/ScriptApi.hpp"

#include "Core/Log.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"
#include "Ui/BoneCatalog.hpp"
#include "Ui/PhotoModeUi.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <algorithm>
#include <new>
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

void DeletePose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    Return(aOut, APM::Pose::PoseOverrides::Delete(name.c_str()));
}

void SaveNewPose(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, CString(APM::Pose::PoseOverrides::SaveNew().c_str()));
}

void PrefGet(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString key;
    float fallback = 0.f;
    RED4ext::GetParameter(aFrame, &key);
    RED4ext::GetParameter(aFrame, &fallback);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::GetPreference(key.c_str(), fallback));
}

void PrefSet(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString key;
    float value = 0.f;
    RED4ext::GetParameter(aFrame, &key);
    RED4ext::GetParameter(aFrame, &value);
    aFrame->code++;
    APM::Ui::PhotoModeUi::SetPreference(key.c_str(), value);
    Return(aOut, true);
}

// Writes a line from the scripts to the plugin log (diagnostics).
void LogInfo(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    CString text;
    RED4ext::GetParameter(aFrame, &text);
    aFrame->code++;
    APM::Log::Info("Script: %s", text.c_str());
    Return(aOut, true);
}

// --- Gizmos ------------------------------------------------------------------------------------------------------

// Model-space position (relative to V's entity: X right, Y forward, Z up) of the main bone of a body part
// (index in the body part list); w = 1 when valid.
void GizmoGetPartPosition(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    RED4ext::Vector4 result{0.f, 0.f, 0.f, 0.f};
    const auto& catalog = APM::Ui::GetBoneCatalog();
    APM::Math::Vec3 position{};
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()) &&
        APM::Pose::GizmoState::GetBoneModelPosition(RED4ext::CName(catalog[part].bones.front()).hash, position))
    {
        result = {position.x, position.y, position.z, 1.f};
    }
    Return(aOut, result);
}

// V's Photo Mode puppet, so scripts can read its world transform. Null when unknown.
void GizmoGetEntity(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;

    RED4ext::Handle<RED4ext::ISerializable> handle;
    const auto entity = APM::Pose::GizmoState::GetEntity();
    if (entity && APM::Pose::TargetRegistry::ContainsPointer(entity))
    {
        // Share the entity's existing reference count instead of creating a new one.
        handle = reinterpret_cast<RED4ext::ISerializable*>(entity)->ref.Lock();
    }
    if (aOut)
    {
        new (aOut) RED4ext::Handle<RED4ext::ISerializable>(handle);
    }
}

// Animated component of V's main skeleton: its GetLocalToWorld() maps bone model space to the world.
void GizmoGetComponent(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;

    RED4ext::Handle<RED4ext::ISerializable> handle;
    const auto component = APM::Pose::GizmoState::GetComponent();
    if (component && APM::Pose::TargetRegistry::ContainsPointer(component))
    {
        handle = reinterpret_cast<RED4ext::ISerializable*>(component)->ref.Lock();
    }
    if (aOut)
    {
        new (aOut) RED4ext::Handle<RED4ext::ISerializable>(handle);
    }
}

// Rotates a body part around a model-space axis (degrees) - used by the gizmos for view-relative rotation.
void GizmoRotatePart(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::Vector4 axis{};
    float degrees = 0.f;
    RED4ext::GetParameter(aFrame, &part);
    RED4ext::GetParameter(aFrame, &axis);
    RED4ext::GetParameter(aFrame, &degrees);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::RotatePart(part, {axis.X, axis.Y, axis.Z}, degrees));
}

// IK chain driven by a body part (0..3), or -1.
void GizmoGetPartIkChain(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t part = 0;
    RED4ext::GetParameter(aFrame, &part);
    aFrame->code++;

    int32_t chain = -1;
    const auto& catalog = APM::Ui::GetBoneCatalog();
    if (part >= 0 && part < static_cast<int32_t>(catalog.size()) && catalog[part].bones.size() == 1)
    {
        const std::string bone = catalog[part].bones.front();
        constexpr const char* ends[] = {"LeftHand", "RightHand", "LeftFoot", "RightFoot"};
        for (int32_t i = 0; i < 4; ++i)
        {
            if (bone == ends[i])
            {
                chain = i;
            }
        }
    }
    Return(aOut, chain);
}

// Moves the IK target of a chain by a model-space vector (meters, X right, Y forward, Z up) and pins the chain.
void GizmoMoveIk(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t chain = 0;
    RED4ext::Vector4 delta{};
    RED4ext::GetParameter(aFrame, &chain);
    RED4ext::GetParameter(aFrame, &delta);
    aFrame->code++;

    if (chain >= 0 && chain < static_cast<int32_t>(APM::Pose::IkChainCount))
    {
        const APM::Math::Vec3 model{delta.X, delta.Y, delta.Z};
        auto settings = APM::Pose::PoseOverrides::GetIkChain(static_cast<APM::Pose::IkChain>(chain));
        settings.enabled = true;
        settings.offset[0] = std::clamp(settings.offset[0] + model.x, -0.8f, 0.8f);
        settings.offset[1] = std::clamp(settings.offset[1] + model.y, -0.8f, 0.8f);
        settings.offset[2] = std::clamp(settings.offset[2] + model.z, -0.8f, 0.8f);
        APM::Pose::PoseOverrides::SetIkChain(static_cast<APM::Pose::IkChain>(chain), settings);
        APM::Ui::PhotoModeUi::RequestRefresh();
    }
    Return(aOut, true);
}

// --- Photo Mode menu (see scripts/AdvancedPoseMod/PhotoModeUi.reds) ----------------------------------------------

void UiIsOwnAttribute(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::IsOwnAttribute(attribute));
}

void UiSetValue(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    uint32_t attribute = 0;
    float value = 0.f;
    RED4ext::GetParameter(aFrame, &attribute);
    RED4ext::GetParameter(aFrame, &value);
    aFrame->code++;
    APM::Ui::PhotoModeUi::SetValue(attribute, value);
}

void UiGetValue(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::GetValue(attribute));
}

void UiIsAttributeEnabled(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::IsAttributeEnabled(attribute));
}

void UiConsumeRefresh(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::ConsumeRefresh());
}

void UiRequestRefresh(IScriptable*, CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    APM::Ui::PhotoModeUi::RequestRefresh();
}

void UiGetOptionCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::GetOptionCount(attribute));
}

void UiGetOptionLabel(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;
    Return(aOut, CString(APM::Ui::PhotoModeUi::GetOptionLabel(attribute, index).c_str()));
}

void UiGetOptionGroup(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    uint32_t attribute = 0;
    int32_t index = 0;
    RED4ext::GetParameter(aFrame, &attribute);
    RED4ext::GetParameter(aFrame, &index);
    aFrame->code++;
    Return(aOut, CString(APM::Ui::PhotoModeUi::GetOptionGroup(attribute, index).c_str()));
}

void UiGetSlotCount(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::GetSlotCount());
}

void UiIsSlotSaved(IScriptable*, CStackFrame* aFrame, void* aOut, int64_t)
{
    int32_t slot = 0;
    RED4ext::GetParameter(aFrame, &slot);
    aFrame->code++;
    Return(aOut, APM::Ui::PhotoModeUi::IsSlotSaved(slot));
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
    ::Register("APM_DeletePose", &DeletePose, "Bool", {{"String", "name"}});
    ::Register("APM_SaveNewPose", &SaveNewPose, "String", {});

    ::Register("APM_LogInfo", &LogInfo, "Bool", {{"String", "text"}});
    ::Register("APM_PrefGet", &PrefGet, "Float", {{"String", "key"}, {"Float", "fallback"}});
    ::Register("APM_PrefSet", &PrefSet, "Bool", {{"String", "key"}, {"Float", "value"}});

    ::Register("APM_GizmoGetPartPosition", &GizmoGetPartPosition, "Vector4", {{"Int32", "part"}});
    ::Register("APM_GizmoGetPartIkChain", &GizmoGetPartIkChain, "Int32", {{"Int32", "part"}});
    ::Register("APM_GizmoMoveIk", &GizmoMoveIk, "Bool", {{"Int32", "chain"}, {"Vector4", "modelDelta"}});
    ::Register("APM_GizmoGetEntity", &GizmoGetEntity, "handle:entEntity", {});
    ::Register("APM_GizmoRotatePart", &GizmoRotatePart, "Bool",
               {{"Int32", "part"}, {"Vector4", "modelAxis"}, {"Float", "degrees"}});
    ::Register("APM_GizmoGetComponent", &GizmoGetComponent, "handle:entIPlacedComponent", {});

    ::Register("APM_UiIsOwnAttribute", &UiIsOwnAttribute, "Bool", {{"Uint32", "attribute"}});
    ::Register("APM_UiSetValue", &UiSetValue, nullptr, {{"Uint32", "attribute"}, {"Float", "value"}});
    ::Register("APM_UiGetValue", &UiGetValue, "Float", {{"Uint32", "attribute"}});
    ::Register("APM_UiIsAttributeEnabled", &UiIsAttributeEnabled, "Bool", {{"Uint32", "attribute"}});
    ::Register("APM_UiConsumeRefresh", &UiConsumeRefresh, "Bool", {});
    ::Register("APM_UiRequestRefresh", &UiRequestRefresh, nullptr, {});
    ::Register("APM_UiGetOptionCount", &UiGetOptionCount, "Int32", {{"Uint32", "attribute"}});
    ::Register("APM_UiGetOptionLabel", &UiGetOptionLabel, "String", {{"Uint32", "attribute"}, {"Int32", "index"}});
    ::Register("APM_UiGetOptionGroup", &UiGetOptionGroup, "String", {{"Uint32", "attribute"}, {"Int32", "index"}});
    ::Register("APM_UiGetSlotCount", &UiGetSlotCount, "Int32", {});
    ::Register("APM_UiIsSlotSaved", &UiIsSlotSaved, "Bool", {{"Int32", "slot"}});

    Log::Info("Script API registered");
}
} // namespace APM::ScriptApi
