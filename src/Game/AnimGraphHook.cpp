#include "Game/AnimGraphHook.hpp"

#include "Core/Config.hpp"
#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Core/SafeMemory.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Math/Quat.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <malloc.h>
#include <mutex>
#include <string>
#include <unordered_set>

namespace
{
// ---------------------------------------------------------------------------------------------------------------------
// Engine layout (found by reverse engineering game 2.31, see docs/RESEARCH.md)
// ---------------------------------------------------------------------------------------------------------------------
// animAnimNode_Base vtable slot 0x128: void Sample(Node* this, Context* ctx, void* a3, SampleOutput* out)
constexpr size_t SampleVtableSlot = 0x128 / sizeof(uintptr_t);

// Expected prologue of animAnimNode_Output::Sample:
//   push rbx / sub rsp, 20h / mov rcx, [rcx+58h] / mov rbx, r9
constexpr std::array<uint8_t, 13> SamplePrologue = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48,
                                                    0x8B, 0x49, 0x58, 0x49, 0x8B, 0xD9};

// Pose buffer (pointed to by SampleOutput::pose)
constexpr uintptr_t PoseBoneCount = 0x24; // uint32
constexpr uintptr_t PoseBones = 0x30;     // QsTransform* (local space)
constexpr uint32_t MaxBones = 2048;

struct SampleOutput
{
    uintptr_t pose;       // pose buffer
    uintptr_t trajectory; // QsTransform* (root motion)
};

using Sample_t = void (*)(void* aNode, void* aContext, void* a3, SampleOutput* aOut);

void* g_addrSample = nullptr;
Sample_t g_origSample = nullptr;

thread_local int t_depth = 0;

// ---------------------------------------------------------------------------------------------------------------------
// Context -> target mapping, learned at runtime by the probe.
// Encoded as (offsetA << 16) | offsetB, where offsetB == 0xFFFF means a direct pointer at ctx+offsetA.
// ---------------------------------------------------------------------------------------------------------------------
constexpr uint32_t PathUnknown = 0xFFFFFFFF;
constexpr uint32_t ProbeScanRange = 0x200;

std::atomic<uint32_t> g_contextPath{PathUnknown};
std::atomic<uint32_t> g_probeBudget{0};
std::atomic<bool> g_poseInfoLogged{false};
std::atomic<bool> g_countMismatchLogged{false};

// Diagnostics (logged periodically while Photo Mode is active)
std::atomic<uint64_t> g_statAllCalls{0};
std::atomic<uint64_t> g_statOuterCalls{0};
std::atomic<uint64_t> g_statContexts{0};
std::atomic<uint64_t> g_statCandidates{0};
std::atomic<uint64_t> g_statLastLogTick{0};
std::atomic<uint32_t> g_candidateDumps{0};
constexpr uint32_t MaxCandidateDumps = 12;

// Distinct pose bone counts seen in new contexts (helps to spot V's graphs).
std::mutex g_boneCountsMutex;
std::array<uint32_t, 48> g_boneCountsSeen{};
size_t g_boneCountsSeenSize = 0;

void RecordBoneCount(uint32_t aCount)
{
    std::scoped_lock lock(g_boneCountsMutex);
    for (size_t i = 0; i < g_boneCountsSeenSize; ++i)
    {
        if (g_boneCountsSeen[i] == aCount)
        {
            return;
        }
    }
    if (g_boneCountsSeenSize < g_boneCountsSeen.size())
    {
        g_boneCountsSeen[g_boneCountsSeenSize++] = aCount;
    }
}

std::string BoneCountsSeen()
{
    std::scoped_lock lock(g_boneCountsMutex);
    std::string text;
    for (size_t i = 0; i < g_boneCountsSeenSize; ++i)
    {
        text += (i ? "," : "") + std::to_string(g_boneCountsSeen[i]);
    }
    return text.empty() ? "-" : text;
}

// Proof-of-concept override (single bone)
std::atomic<bool> g_testEnabled{false};
std::atomic<uint64_t> g_testBoneHash{0};
APM::Math::Quat g_testRotation{};

uintptr_t ResolveTargetFromContext(uintptr_t aContext, uint32_t aPath)
{
    const uint32_t offsetA = aPath >> 16;
    const uint32_t offsetB = aPath & 0xFFFF;

    uint64_t value = 0;
    if (!APM::SafeMemory::ReadU64(aContext + offsetA, value))
    {
        return 0;
    }
    if (offsetB == 0xFFFF)
    {
        return value;
    }

    uint64_t inner = 0;
    if (!APM::SafeMemory::ReadU64(value + offsetB, inner))
    {
        return 0;
    }
    return inner;
}

bool IsProbePointer(uint64_t aValue, const std::array<uintptr_t, 64>& aPointers, size_t aCount)
{
    for (size_t i = 0; i < aCount; ++i)
    {
        if (aPointers[i] == aValue)
        {
            return true;
        }
    }
    return false;
}

// Each distinct context is scanned only once per session.
std::mutex g_visitedMutex;
std::unordered_set<uintptr_t> g_visitedContexts;

bool MarkContextVisited(uintptr_t aContext)
{
    std::scoped_lock lock(g_visitedMutex);
    return g_visitedContexts.insert(aContext).second;
}

// Reads a block; if the full range is not readable, retries with the part up to the next page boundary.
size_t ReadBlock(uintptr_t aAddress, std::array<uint64_t, ProbeScanRange / 8>& aOut)
{
    if (APM::SafeMemory::ReadBytes(aAddress, aOut.data(), ProbeScanRange))
    {
        return aOut.size();
    }
    const size_t toPageEnd = 0x1000 - (aAddress & 0xFFF);
    const size_t size = (toPageEnd / 8) * 8;
    if (size >= 8 && size < ProbeScanRange && APM::SafeMemory::ReadBytes(aAddress, aOut.data(), size))
    {
        return size / 8;
    }
    return 0;
}

// Looks for a pointer to a registered entity/component inside the context (depth 2).
void ProbeContext(uintptr_t aContext)
{
    std::array<uintptr_t, 64> pointers{};
    const size_t count = APM::Pose::TargetRegistry::GetProbePointers(pointers);
    if (count == 0)
    {
        return;
    }

    std::array<uint64_t, ProbeScanRange / 8> level1{};
    std::array<uint64_t, ProbeScanRange / 8> level2{};
    const size_t count1 = ReadBlock(aContext, level1);

    for (size_t i = 0; i < count1; ++i)
    {
        const uint32_t a = static_cast<uint32_t>(i * 8);
        const uint64_t first = level1[i];

        if (IsProbePointer(first, pointers, count))
        {
            const uint32_t path = (a << 16) | 0xFFFF;
            uint32_t expected = PathUnknown;
            if (g_contextPath.compare_exchange_strong(expected, path))
            {
                APM::Log::Info("Probe: context %p -> [+0x%X] = %p (target pointer)", reinterpret_cast<void*>(aContext),
                               a, reinterpret_cast<void*>(first));
            }
            return;
        }

        if (!APM::SafeMemory::LooksLikePointer(first) || APM::SafeMemory::IsInGameImage(first))
        {
            continue;
        }

        const size_t count2 = ReadBlock(first, level2);
        for (size_t j = 0; j < count2; ++j)
        {
            const uint32_t b = static_cast<uint32_t>(j * 8);
            const uint64_t second = level2[j];
            if (IsProbePointer(second, pointers, count))
            {
                const uint32_t path = (a << 16) | b;
                uint32_t expected = PathUnknown;
                if (g_contextPath.compare_exchange_strong(expected, path))
                {
                    APM::Log::Info("Probe: context %p -> [+0x%X] = %p -> [+0x%X] = %p (target pointer)",
                                   reinterpret_cast<void*>(aContext), a, reinterpret_cast<void*>(first), b,
                                   reinterpret_cast<void*>(second));
                }
                return;
            }
        }
    }
}

std::string HexDump(uintptr_t aAddress, size_t aSize)
{
    std::string text;
    std::array<uint8_t, 0x100> buffer{};
    aSize = (aSize < buffer.size()) ? aSize : buffer.size();
    if (!APM::SafeMemory::ReadBytes(aAddress, buffer.data(), aSize))
    {
        return "<unreadable>";
    }

    char chunk[8]{};
    for (size_t i = 0; i < aSize; ++i)
    {
        if (i && (i % 16) == 0)
        {
            text += " |";
        }
        std::snprintf(chunk, sizeof(chunk), " %02X", buffer[i]);
        text += chunk;
    }
    return text;
}

void LogPoseInfo(void* aNode, uintptr_t aContext, const SampleOutput* aOut, const APM::Pose::Target& aTarget)
{
    bool expected = false;
    if (!g_poseInfoLogged.compare_exchange_strong(expected, true))
    {
        return;
    }

    uint64_t nodeVtbl = 0;
    APM::SafeMemory::ReadU64(reinterpret_cast<uintptr_t>(aNode), nodeVtbl);
    uint32_t boneCount = 0;
    APM::SafeMemory::ReadU32(aOut->pose + PoseBoneCount, boneCount);

    APM::Log::Info("Probe: target=%s node=%p vtbl=+0x%llX ctx=%p pose=%p trajectory=%p poseBones=%u rigBones=%zu",
                   aTarget.debugName.c_str(), aNode,
                   static_cast<unsigned long long>(nodeVtbl - APM::SafeMemory::GetGameImageBase()),
                   reinterpret_cast<void*>(aContext), reinterpret_cast<void*>(aOut->pose),
                   reinterpret_cast<void*>(aOut->trajectory), boneCount, aTarget.boneNames.size());
    APM::Log::Info("Probe: pose header:%s", HexDump(aOut->pose, 0x50).c_str());
    APM::Log::Info("Probe: context head:%s", HexDump(aContext, 0x80).c_str());
}

void ApplyTestOverride(const SampleOutput* aOut, uintptr_t aTargetPointer, const APM::Pose::Target& aTarget)
{
    if (!g_testEnabled.load(std::memory_order_acquire))
    {
        return;
    }

    uint32_t boneCount = 0;
    uint64_t bonesAddress = 0;
    if (!APM::SafeMemory::ReadU32(aOut->pose + PoseBoneCount, boneCount) ||
        !APM::SafeMemory::ReadU64(aOut->pose + PoseBones, bonesAddress) || boneCount == 0 || boneCount > MaxBones)
    {
        return;
    }

    if (boneCount != aTarget.boneNames.size())
    {
        bool expected = false;
        if (g_countMismatchLogged.compare_exchange_strong(expected, true))
        {
            APM::Log::Warn("Pose bone count (%u) differs from rig bone count (%zu); override disabled", boneCount,
                           aTarget.boneNames.size());
        }
        return;
    }

    const int32_t index = APM::Pose::TargetRegistry::FindBoneIndex(aTargetPointer, g_testBoneHash.load());
    if (index < 0 || static_cast<uint32_t>(index) >= boneCount)
    {
        return;
    }

    auto* bones = reinterpret_cast<RED4ext::QsTransform*>(bonesAddress);
    auto& rotation = bones[index].Rotation;

    APM::Math::Quat current{rotation.i, rotation.j, rotation.k, rotation.r};
    const auto result = APM::Math::Normalize(APM::Math::Mul(current, g_testRotation));
    rotation.i = result.i;
    rotation.j = result.j;
    rotation.k = result.k;
    rotation.r = result.r;
}

// Describes a qword: plain value, pointer into the game image, or heap object with a vtable in the image.
std::string DescribeValue(uint64_t aValue)
{
    char text[96]{};
    const auto base = APM::SafeMemory::GetGameImageBase();
    if (APM::SafeMemory::IsInGameImage(aValue))
    {
        std::snprintf(text, sizeof(text), "image+0x%llX", static_cast<unsigned long long>(aValue - base));
        return text;
    }
    uint64_t vtbl = 0;
    if (APM::SafeMemory::LooksLikePointer(aValue) && APM::SafeMemory::ReadU64(aValue, vtbl) &&
        APM::SafeMemory::IsInGameImage(vtbl))
    {
        std::snprintf(text, sizeof(text), "%p (object, vtbl=+0x%llX)", reinterpret_cast<void*>(aValue),
                      static_cast<unsigned long long>(vtbl - base));
        return text;
    }
    std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(aValue));
    return text;
}

// A context whose pose has the same bone count as one of V's rigs is a strong candidate.
// Dump its layout so the mapping can be found offline.
void DumpCandidate(void* aNode, uintptr_t aContext, const SampleOutput* aOut, uint32_t aBoneCount)
{
    if (g_candidateDumps.fetch_add(1) >= MaxCandidateDumps)
    {
        return;
    }

    uint64_t nodeVtbl = 0;
    APM::SafeMemory::ReadU64(reinterpret_cast<uintptr_t>(aNode), nodeVtbl);
    APM::Log::Info("Candidate: ctx=%p bones=%u node=%p nodeVtbl=+0x%llX pose=%p", reinterpret_cast<void*>(aContext),
                   aBoneCount, aNode,
                   static_cast<unsigned long long>(nodeVtbl - APM::SafeMemory::GetGameImageBase()),
                   reinterpret_cast<void*>(aOut->pose));

    std::array<uint64_t, ProbeScanRange / 8> block{};
    const size_t count = ReadBlock(aContext, block);
    for (size_t i = 0; i < count && i < 0x100 / 8; ++i)
    {
        APM::Log::Info("Candidate:   ctx+0x%03zX = %s", i * 8, DescribeValue(block[i]).c_str());
    }
}

void LogStatsPeriodically()
{
    const uint64_t now = GetTickCount64();
    uint64_t last = g_statLastLogTick.load();
    if (now - last < 3000 || !g_statLastLogTick.compare_exchange_strong(last, now))
    {
        return;
    }
    APM::Log::Info("Stats: sampleCalls=%llu outermost=%llu contextsScanned=%llu candidates=%llu mapping=%s boneCounts=[%s]",
                   static_cast<unsigned long long>(g_statAllCalls.load()),
                   static_cast<unsigned long long>(g_statOuterCalls.load()),
                   static_cast<unsigned long long>(g_statContexts.load()),
                   static_cast<unsigned long long>(g_statCandidates.load()),
                   g_contextPath.load() == PathUnknown ? "unknown" : "found", BoneCountsSeen().c_str());
}

void OnPoseSampled(void* aNode, void* aContext, SampleOutput* aOut)
{
    if (!aOut || !aOut->pose || APM::Pose::TargetRegistry::Count() == 0)
    {
        return;
    }

    const auto context = reinterpret_cast<uintptr_t>(aContext);
    const uint32_t path = g_contextPath.load(std::memory_order_acquire);

    if (path == PathUnknown)
    {
        // Probe phase: every distinct context is scanned once, up to the budget.
        if (g_probeBudget.load() == 0 || !MarkContextVisited(context))
        {
            return;
        }
        if (g_probeBudget.fetch_sub(1) == 1)
        {
            APM::Log::Warn("Probe: budget exhausted, context mapping not found");
        }
        g_statContexts.fetch_add(1);

        uint32_t boneCount = 0;
        const bool hasCount = APM::SafeMemory::ReadU32(aOut->pose + PoseBoneCount, boneCount);
        if (hasCount)
        {
            RecordBoneCount(boneCount);
        }
        if (hasCount && APM::Pose::TargetRegistry::HasBoneCount(boneCount))
        {
            g_statCandidates.fetch_add(1);
            DumpCandidate(aNode, context, aOut, boneCount);
        }

        ProbeContext(context);
        return;
    }

    const uintptr_t targetPointer = ResolveTargetFromContext(context, path);
    if (!APM::Pose::TargetRegistry::ContainsPointer(targetPointer))
    {
        return;
    }

    APM::Pose::Target target;
    if (!APM::Pose::TargetRegistry::FindByPointer(targetPointer, target))
    {
        return;
    }

    LogPoseInfo(aNode, context, aOut, target);
    ApplyTestOverride(aOut, targetPointer, target);
}

void Detour_Sample(void* aNode, void* aContext, void* a3, SampleOutput* aOut)
{
    ++t_depth;
    g_origSample(aNode, aContext, a3, aOut);
    --t_depth;

    if (!APM::PhotoModeTracker::IsActive())
    {
        return;
    }

    g_statAllCalls.fetch_add(1, std::memory_order_relaxed);
    LogStatsPeriodically();

    // Only the outermost output node carries the final pose of a graph.
    if (t_depth != 0)
    {
        return;
    }
    g_statOuterCalls.fetch_add(1, std::memory_order_relaxed);

    OnPoseSampled(aNode, aContext, aOut);
}

uintptr_t ResolveOutputNodeVtable()
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* cls = rtti ? rtti->GetClass("animAnimNode_Output") : nullptr;
    if (!cls)
    {
        APM::Log::Error("AnimGraph: animAnimNode_Output class not found");
        return 0;
    }

    const size_t size = cls->GetSize();
    const size_t alignment = cls->GetAlignment() < 16 ? 16 : cls->GetAlignment();
    void* memory = _aligned_malloc(size, alignment);
    if (!memory)
    {
        return 0;
    }

    std::memset(memory, 0, size);
    cls->ConstructCls(memory);
    const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(memory);
    cls->DestructCls(memory);
    _aligned_free(memory);

    return vtable;
}
} // namespace

namespace APM::AnimGraphHook
{
bool Install()
{
    const uintptr_t vtable = ResolveOutputNodeVtable();
    if (!vtable || !SafeMemory::IsInGameImage(vtable))
    {
        Log::Error("AnimGraph: invalid animAnimNode_Output vtable (%p)", reinterpret_cast<void*>(vtable));
        return false;
    }

    const uintptr_t sample = reinterpret_cast<uintptr_t*>(vtable)[SampleVtableSlot];
    const auto base = SafeMemory::GetGameImageBase();
    Log::Info("AnimGraph: animAnimNode_Output vtable=+0x%llX Sample=+0x%llX",
              static_cast<unsigned long long>(vtable - base), static_cast<unsigned long long>(sample - base));

    if (!SafeMemory::IsInGameImage(sample) ||
        std::memcmp(reinterpret_cast<const void*>(sample), SamplePrologue.data(), SamplePrologue.size()) != 0)
    {
        Log::Error("AnimGraph: Sample() prologue mismatch, the game was probably updated. Hook NOT installed.");
        Log::Error("AnimGraph: found bytes:%s", HexDump(sample, 16).c_str());
        return false;
    }

    g_addrSample = reinterpret_cast<void*>(sample);
    if (!AttachHook(g_addrSample, &Detour_Sample, &g_origSample))
    {
        Log::Error("AnimGraph: failed to attach Sample() hook");
        g_addrSample = nullptr;
        return false;
    }

    Log::Info("AnimGraph: Sample() hook installed");
    return true;
}

void Uninstall()
{
    if (g_addrSample)
    {
        DetachHook(g_addrSample);
        g_addrSample = nullptr;
    }
}

void OnPhotoModeActivated()
{
    const auto config = Config::Get();

    g_testEnabled.store(false, std::memory_order_release);
    g_testRotation = Math::FromEulerXYZDegrees(config.testRotX, config.testRotY, config.testRotZ);
    g_testBoneHash = RED4ext::CName(config.testBone.c_str()).hash;
    g_testEnabled.store(config.testEnabled, std::memory_order_release);

    g_poseInfoLogged = false;
    g_countMismatchLogged = false;
    {
        std::scoped_lock lock(g_visitedMutex);
        g_visitedContexts.clear();
    }
    g_probeBudget = config.probeEnabled ? config.probeScanBudget : 0;
    g_statAllCalls = 0;
    g_statOuterCalls = 0;
    g_statContexts = 0;
    g_statCandidates = 0;
    g_candidateDumps = 0;
    {
        std::scoped_lock lock(g_boneCountsMutex);
        g_boneCountsSeenSize = 0;
    }

    // Keep a mapping learned in a previous session; it does not change while the game runs.
    if (g_contextPath.load() == PathUnknown && !config.probeEnabled)
    {
        Log::Warn("Probe disabled and context mapping unknown: overrides cannot be applied");
    }
}
} // namespace APM::AnimGraphHook
