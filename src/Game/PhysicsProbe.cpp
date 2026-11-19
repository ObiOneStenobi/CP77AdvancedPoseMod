#include "Game/PhysicsProbe.hpp"

#include "Core/Config.hpp"
#include "Core/CrashGuard.hpp"
#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Core/SafeMemory.hpp"
#include "Game/AnimGraphHook.hpp"
#include "Game/EngineLayout.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Math/Quat.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/RED4ext.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
using namespace APM;

using Sample_t = void (*)(void* aNode, void* aContext, void* a3, void* aOut);

constexpr size_t MaxHooks = 16;
constexpr uint64_t MotionWindowMs = 30000; // motion logged during the first 30 s of a session
constexpr uint64_t MotionStepMs = 500;
constexpr uint64_t StickLogMs = 1000;

struct NodeHook
{
    std::string className;
    void* address = nullptr;
    Sample_t original = nullptr;
};
std::array<NodeHook, MaxHooks> g_hooks;
size_t g_hookCount = 0;
std::atomic<bool> g_enabled{false};

struct Motion
{
    std::vector<Math::Quat> last;
    uint64_t tick = 0;
    int32_t testIndex = -2; // -2 not looked up, -1 not in this skeleton
    Math::Quat written;
    bool hasWritten = false;
    uint64_t stickTick = 0;
};

std::mutex g_mutex;
std::unordered_set<uint64_t> g_nodesSeen;      // hook index and component
std::unordered_set<uint64_t> g_puppetsLogged;
std::unordered_map<uintptr_t, Motion> g_motion; // by animated component
uint64_t g_sessionStart = 0;
uint64_t g_testBone = 0;
float g_testAngle = 0.f;

std::string NameOf(uint64_t aHash)
{
    const char* text = RED4ext::CName(aHash).ToString();
    return text ? std::string(text) : std::string("?");
}

std::string Lower(std::string aText)
{
    std::transform(aText.begin(), aText.end(), aText.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return aText;
}

bool LooksLikePhysics(const std::string& aClass)
{
    static constexpr const char* Words[] = {"dangle", "dyng", "physic", "cloth", "ragdoll", "simul",
                                            "spring", "pendul", "jiggle", "wind",   "dynamic"};
    const auto lower = Lower(aClass);
    for (const char* word : Words)
    {
        if (lower.find(word) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

// Skeletons the probe leaves out: the main one and those that only take its bones or the face.
bool IsSecondary(const std::string& aName)
{
    return aName != "root" && aName != "face_rig" && aName != "deformations" && aName != "shadow";
}

float AngleDegrees(const Math::Quat& a, const Math::Quat& b)
{
    const float dot = std::fabs(a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r);
    return 2.f * std::acos(std::min(1.f, dot)) * 57.29578f;
}

Math::Quat ToQuat(const RED4ext::QsTransform& aTransform)
{
    return {aTransform.Rotation.i, aTransform.Rotation.j, aTransform.Rotation.k, aTransform.Rotation.r};
}

void NoteNode(size_t aHook, uintptr_t aContext)
{
    const uintptr_t component = AnimGraphHook::ComponentFromContext(aContext);
    if (!component || !Pose::TargetRegistry::ContainsPointer(component))
    {
        return; // not a Photo Mode character
    }
    const uint64_t key = (static_cast<uint64_t>(aHook) << 56) ^ component;
    {
        std::scoped_lock lock(g_mutex);
        if (!g_nodesSeen.insert(key).second)
        {
            return;
        }
    }
    Pose::Target target;
    if (!Pose::TargetRegistry::FindByPointer(component, target))
    {
        return;
    }
    Log::Info("PhysicsProbe: node %s runs in character %u skeleton %s (%zu bones), inside the graph output: %s",
              g_hooks[aHook].className.c_str(), target.photoModeSlot, target.debugName.c_str(), target.boneNames.size(),
              AnimGraphHook::OutputDepth() > 0 ? "yes" : "no");
}

template<size_t I>
void Detour_Node(void* aNode, void* aContext, void* a3, void* aOut)
{
    g_hooks[I].original(aNode, aContext, a3, aOut);
    if (!g_enabled.load(std::memory_order_relaxed) || !PhotoModeTracker::IsActive() || CrashGuard::ShuttingDown())
    {
        return;
    }
    CrashGuard::Guard("physics probe (animation node)", [&] { NoteNode(I, reinterpret_cast<uintptr_t>(aContext)); });
}

template<size_t... I>
constexpr std::array<Sample_t, sizeof...(I)> MakeDetours(std::index_sequence<I...>)
{
    return {&Detour_Node<I>...};
}
constexpr auto Detours = MakeDetours(std::make_index_sequence<MaxHooks>{});

// The chains of a secondary skeleton: bones of its own (not in the main skeleton) whose parent is a shared bone (or
// none), each with the bones hanging from it.
void LogChains(uint32_t aCharacter, const Pose::Target& aTarget, const std::unordered_set<uint64_t>& aMainBones)
{
    const size_t count = aTarget.boneNames.size();
    std::vector<std::vector<int32_t>> children(count);
    size_t shared = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (aMainBones.contains(aTarget.boneNames[i]))
        {
            ++shared;
        }
        const int32_t parent = i < aTarget.parents.size() ? aTarget.parents[i] : -1;
        if (parent >= 0 && static_cast<size_t>(parent) < count)
        {
            children[parent].push_back(static_cast<int32_t>(i));
        }
    }
    std::vector<int32_t> roots;
    for (size_t i = 0; i < count; ++i)
    {
        if (aMainBones.contains(aTarget.boneNames[i]))
        {
            continue;
        }
        const int32_t parent = i < aTarget.parents.size() ? aTarget.parents[i] : -1;
        if (parent < 0 || aMainBones.contains(aTarget.boneNames[parent]))
        {
            roots.push_back(static_cast<int32_t>(i));
        }
    }
    Log::Info("PhysicsProbe: character %u skeleton %s: %zu bones, %zu shared with the main skeleton, %zu chains",
              aCharacter, aTarget.debugName.c_str(), count, shared, roots.size());
    constexpr size_t MaxChains = 32;
    for (size_t r = 0; r < roots.size() && r < MaxChains; ++r)
    {
        const int32_t root = roots[r];
        // Size, depth and tips of the chain.
        size_t bones = 0;
        size_t tips = 0;
        size_t depth = 0;
        std::vector<std::pair<int32_t, size_t>> stack{{root, 1}};
        int32_t deepest = root;
        while (!stack.empty())
        {
            const auto [bone, level] = stack.back();
            stack.pop_back();
            ++bones;
            if (level > depth)
            {
                depth = level;
                deepest = bone;
            }
            if (children[bone].empty())
            {
                ++tips;
            }
            for (const int32_t child : children[bone])
            {
                stack.push_back({child, level + 1});
            }
        }
        const int32_t parent = root < static_cast<int32_t>(aTarget.parents.size()) ? aTarget.parents[root] : -1;
        Log::Info("PhysicsProbe:   chain %s on %s: %zu bones, %zu long, %zu tips (deepest %s)",
                  NameOf(aTarget.boneNames[root]).c_str(), parent >= 0 ? NameOf(aTarget.boneNames[parent]).c_str() : "(none)",
                  bones, depth, tips, NameOf(aTarget.boneNames[deepest]).c_str());
    }
    if (roots.size() > MaxChains)
    {
        Log::Info("PhysicsProbe:   ... %zu more chains", roots.size() - MaxChains);
    }
}
} // namespace

namespace APM::PhysicsProbe
{
void Install()
{
    if (!Config::Get().probePhysics)
    {
        return;
    }
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* base = rtti ? rtti->GetClass("animAnimNode_Base") : nullptr;
    if (!base)
    {
        Log::Warn("PhysicsProbe: animAnimNode_Base not found");
        return;
    }
    RED4ext::DynArray<RED4ext::CClass*> classes;
    rtti->GetClasses(base, classes, nullptr, false);

    std::vector<std::string> names;
    std::vector<std::string> physics;
    for (auto* cls : classes)
    {
        const char* text = cls ? cls->GetName().ToString() : nullptr;
        if (!text)
        {
            continue;
        }
        names.emplace_back(text);
        if (LooksLikePhysics(text))
        {
            physics.emplace_back(text);
        }
    }
    std::sort(names.begin(), names.end());
    std::string line;
    for (size_t i = 0; i < names.size(); ++i)
    {
        line += (line.empty() ? "" : ", ") + names[i];
        if (line.size() > 400 || i + 1 == names.size())
        {
            Log::Info("PhysicsProbe: node classes: %s", line.c_str());
            line.clear();
        }
    }
    Log::Info("PhysicsProbe: %zu animation node classes, %zu look like physics", names.size(), physics.size());

    const auto imageBase = SafeMemory::GetGameImageBase();
    std::unordered_set<uintptr_t> used{AnimGraphHook::NodeSample(AnimGraphHook::NodeVtable("animAnimNode_Output")),
                                       AnimGraphHook::NodeSample(AnimGraphHook::NodeVtable("animAnimNode_Sermo"))};
    for (const auto& name : physics)
    {
        if (g_hookCount >= MaxHooks)
        {
            Log::Info("PhysicsProbe: %s not hooked (at most %zu)", name.c_str(), MaxHooks);
            continue;
        }
        const uintptr_t sample = AnimGraphHook::NodeSample(AnimGraphHook::NodeVtable(name.c_str()));
        if (!sample || !used.insert(sample).second)
        {
            Log::Info("PhysicsProbe: %s not hooked (%s)", name.c_str(),
                      sample ? "same Sample as an already hooked class" : "no Sample found");
            continue;
        }
        auto& hook = g_hooks[g_hookCount];
        hook.className = name;
        hook.address = reinterpret_cast<void*>(sample);
        if (!AttachHook(hook.address, Detours[g_hookCount], &hook.original))
        {
            Log::Warn("PhysicsProbe: failed to hook %s", name.c_str());
            hook = {};
            continue;
        }
        Log::Info("PhysicsProbe: hooked %s Sample=+0x%llX", name.c_str(),
                  static_cast<unsigned long long>(sample - imageBase));
        ++g_hookCount;
    }
    g_enabled = true;
}

void Uninstall()
{
    g_enabled = false;
    for (size_t i = 0; i < g_hookCount; ++i)
    {
        if (g_hooks[i].address)
        {
            DetachHook(g_hooks[i].address);
            g_hooks[i].address = nullptr;
        }
    }
    g_hookCount = 0;
}

bool Enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

void OnPhotoModeActivated()
{
    if (!Enabled())
    {
        return;
    }
    const auto config = Config::Get();
    std::scoped_lock lock(g_mutex);
    g_nodesSeen.clear();
    g_puppetsLogged.clear();
    g_motion.clear();
    g_sessionStart = GetTickCount64();
    g_testBone = config.probePhysicsTestBone.empty() ? 0 : RED4ext::CName(config.probePhysicsTestBone.c_str()).hash;
    g_testAngle = config.probePhysicsTestAngle;
}

void LogPuppet(uint64_t aPuppet, uint32_t aCharacter, const std::vector<Pose::Target>& aTargets, size_t aFirst)
{
    if (!Enabled() || aFirst >= aTargets.size())
    {
        return;
    }
    {
        std::scoped_lock lock(g_mutex);
        if (!g_puppetsLogged.insert(aPuppet).second)
        {
            return;
        }
    }
    // Every component: the clothes may simulate outside the animation (cloth or physical mesh components).
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* animatedCls = rtti ? rtti->GetClass("entAnimatedComponent") : nullptr;
    const auto* graphProp = animatedCls ? animatedCls->GetProperty("graph") : nullptr;
    EngineLayout::ComponentList components;
    if (EngineLayout::GetComponents(aPuppet, components))
    {
        Log::Info("PhysicsProbe: character %u (puppet %p): %u components", aCharacter, reinterpret_cast<void*>(aPuppet),
                  components.size);
        for (uint32_t c = 0; c < components.size; ++c)
        {
            const uintptr_t component = EngineLayout::GetComponent(components, c);
            if (!component || !EngineLayout::LooksAlive(component))
            {
                continue;
            }
            auto* cls = reinterpret_cast<RED4ext::ISerializable*>(component)->GetType();
            const char* clsName = cls ? cls->GetName().ToString() : nullptr;
            const std::string name = NameOf(EngineLayout::GetComponentName(component).hash);
            uint64_t graph = 0;
            if (graphProp && cls && animatedCls && cls->IsA(animatedCls))
            {
                SafeMemory::ReadU64(component + graphProp->valueOffset, graph);
                Log::Info("PhysicsProbe:   %s %s graph %016llx", clsName ? clsName : "?", name.c_str(),
                          static_cast<unsigned long long>(graph));
            }
            else
            {
                Log::Info("PhysicsProbe:   %s %s", clsName ? clsName : "?", name.c_str());
            }
        }
    }
    // Chains of the secondary skeletons, against the main one.
    std::unordered_set<uint64_t> mainBones;
    for (size_t t = aFirst; t < aTargets.size(); ++t)
    {
        if (aTargets[t].debugName == "root")
        {
            mainBones.insert(aTargets[t].boneNames.begin(), aTargets[t].boneNames.end());
        }
    }
    for (size_t t = aFirst; t < aTargets.size(); ++t)
    {
        if (IsSecondary(aTargets[t].debugName) && !aTargets[t].boneNames.empty())
        {
            LogChains(aCharacter, aTargets[t], mainBones);
        }
    }
}

void OnSkeletonSampled(const Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    if (!Enabled() || !IsSecondary(aTarget.debugName))
    {
        return;
    }
    const uint64_t now = GetTickCount64();
    std::scoped_lock lock(g_mutex);
    auto& motion = g_motion[aTarget.animatedComponent];

    // How much the bones turned since the last step (body still with the pose on: the physics is what moves them).
    if (now - g_sessionStart <= MotionWindowMs)
    {
        if (motion.last.size() != aCount)
        {
            motion.last.resize(aCount);
            for (uint32_t i = 0; i < aCount; ++i)
            {
                motion.last[i] = ToQuat(aBones[i]);
            }
            motion.tick = now;
        }
        else if (now - motion.tick >= MotionStepMs)
        {
            float most = 0.f;
            uint32_t mostBone = 0;
            for (uint32_t i = 0; i < aCount; ++i)
            {
                const auto rotation = ToQuat(aBones[i]);
                const float angle = AngleDegrees(rotation, motion.last[i]);
                if (angle > most)
                {
                    most = angle;
                    mostBone = i;
                }
                motion.last[i] = rotation;
            }
            Log::Info("PhysicsProbe: character %u %s: most turned bone %s %.2f degrees in %.1f s (pose %s)",
                      aTarget.photoModeSlot, aTarget.debugName.c_str(),
                      mostBone < aTarget.boneNames.size() ? NameOf(aTarget.boneNames[mostBone]).c_str() : "?", most,
                      static_cast<float>(now - motion.tick) / 1000.f,
                      Pose::PoseOverrides::IsEnabled(aTarget.photoModeSlot) ? "on" : "off");
            motion.tick = now;
        }
    }

    // Test bone: turned every frame; the next frame shows whether the physics started from the turned bone.
    if (g_testBone == 0 || g_testAngle == 0.f)
    {
        return;
    }
    if (motion.testIndex == -2)
    {
        motion.testIndex = -1;
        for (size_t i = 0; i < aTarget.boneNames.size() && i < aCount; ++i)
        {
            if (aTarget.boneNames[i] == g_testBone)
            {
                motion.testIndex = static_cast<int32_t>(i);
                Log::Info("PhysicsProbe: test bone %s found in character %u %s (index %zu), turned by %.1f degrees",
                          NameOf(g_testBone).c_str(), aTarget.photoModeSlot, aTarget.debugName.c_str(), i, g_testAngle);
                break;
            }
        }
    }
    if (motion.testIndex < 0)
    {
        return;
    }
    auto& bone = aBones[motion.testIndex];
    const auto incoming = ToQuat(bone);
    if (motion.hasWritten && now - motion.stickTick >= StickLogMs)
    {
        motion.stickTick = now;
        Log::Info("PhysicsProbe: test bone on %s: incoming %.1f degrees from our last write (0 = the physics continues "
                  "from the turned bone)",
                  aTarget.debugName.c_str(), AngleDegrees(incoming, motion.written));
    }
    const auto written =
        Math::Normalize(Math::Mul(incoming, Math::FromAxisAngle(0.f, 0.f, 1.f, Math::DegToRad(g_testAngle))));
    bone.Rotation.i = written.i;
    bone.Rotation.j = written.j;
    bone.Rotation.k = written.k;
    bone.Rotation.r = written.r;
    motion.written = written;
    motion.hasWritten = true;
}
} // namespace APM::PhysicsProbe
