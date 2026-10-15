#include "Game/AnimGraphHook.hpp"

#include "Core/Config.hpp"
#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Core/SafeMemory.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Math/Quat.hpp"
#include "Pose/FaceRig.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/PoseProcessor.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <malloc.h>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

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

// Known mapping for game 2.31 (found by the probe):
//   ctx+0x48 -> runtime animated object, +0x118 -> entAnimatedComponent
// If it stops resolving (game update), the plugin falls back to the probe automatically.
constexpr uint32_t KnownContextPath = (0x48u << 16) | 0x118u;
constexpr uint32_t PathValidationMisses = 500;
std::atomic<uint32_t> g_pathHits{0};
std::atomic<uint32_t> g_pathMisses{0};
constexpr uint32_t ProbeScanRange = 0x200;

std::atomic<uint32_t> g_contextPath{KnownContextPath};
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

        if (APM::Pose::TargetRegistry::ContainsPointer(first))
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
            if (APM::Pose::TargetRegistry::ContainsPointer(second))
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

    APM::Log::Verbose("Probe: target=%s node=%p vtbl=+0x%llX ctx=%p pose=%p trajectory=%p poseBones=%u rigBones=%zu",
                   aTarget.debugName.c_str(), aNode,
                   static_cast<unsigned long long>(nodeVtbl - APM::SafeMemory::GetGameImageBase()),
                   reinterpret_cast<void*>(aContext), reinterpret_cast<void*>(aOut->pose),
                   reinterpret_cast<void*>(aOut->trajectory), boneCount, aTarget.boneNames.size());
    APM::Log::Verbose("Probe: pose header:%s", HexDump(aOut->pose, 0x50).c_str());
    APM::Log::Verbose("Probe: context head:%s", HexDump(aContext, 0x80).c_str());
}

// Applies the user's bone overrides (additive) to the final local-space pose of a target.
std::atomic<bool> g_invalidPoseLogged{false};

bool AllFinite(const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    for (uint32_t i = 0; i < aCount; ++i)
    {
        const auto& t = aBones[i];
        const float values[] = {t.Translation.X, t.Translation.Y, t.Translation.Z, t.Rotation.i, t.Rotation.j,
                                t.Rotation.k,    t.Rotation.r,    t.Scale.X,       t.Scale.Y,    t.Scale.Z};
        for (const float v : values)
        {
            if (!std::isfinite(v))
            {
                return false;
            }
        }
    }
    return true;
}

void ApplyOverrides(void* aNode, const SampleOutput* aOut, uintptr_t aTargetPointer)
{
    const auto snapshot = APM::Pose::PoseOverrides::GetSnapshot();
    const bool applyPose = APM::Pose::PoseOverrides::IsEnabled() && !snapshot->IsEmpty();

    uint32_t boneCount = 0;
    uint64_t bonesAddress = 0;
    if (!APM::SafeMemory::ReadU32(aOut->pose + PoseBoneCount, boneCount) ||
        !APM::SafeMemory::ReadU64(aOut->pose + PoseBones, bonesAddress) || boneCount == 0 || boneCount > MaxBones)
    {
        return;
    }

    APM::Pose::TargetRegistry::VisitTarget(aTargetPointer, [&](const APM::Pose::Target& aTarget) {
        if (aTarget.photoModeSlot != 0)
        {
            return; // only V is posed: other Photo Mode characters keep the game's animation
        }
        if (boneCount != aTarget.boneNames.size())
        {
            bool expected = false;
            if (g_countMismatchLogged.compare_exchange_strong(expected, true))
            {
                APM::Log::Warn("Pose bone count (%u) differs from rig bone count (%zu) on %s; skipped", boneCount,
                               aTarget.boneNames.size(), aTarget.debugName.c_str());
            }
            return;
        }

        auto* bones = reinterpret_cast<RED4ext::QsTransform*>(bonesAddress);
        if (aTarget.debugName == "face_rig")
        {
            // Reads the expression shown (face controls start from it).
            APM::Pose::FaceRig::Observe(aTarget, bones, boneCount, *snapshot);
        }
        if (applyPose)
        {
            // A frame with invalid values (NaN / infinite, e.g. a degenerate IK case) keeps the game's pose instead.
            thread_local std::vector<RED4ext::QsTransform> before;
            before.assign(bones, bones + boneCount);
            APM::Pose::PoseProcessor::Process(aTarget, bones, boneCount, *snapshot);
            if (!AllFinite(bones, boneCount))
            {
                std::copy(before.begin(), before.end(), bones);
                bool expected = false;
                if (g_invalidPoseLogged.compare_exchange_strong(expected, true))
                {
                    APM::Log::Warn("Invalid bone values on %s: frame left as the game animated it", aTarget.debugName.c_str());
                }
            }
        }
        // The gizmos follow the final pose, edited or not.
        APM::Pose::GizmoState::Publish(aTarget, bones, boneCount, reinterpret_cast<uintptr_t>(aNode));
    });
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

// Scans a memory range for registered target pointers (depth 1 into heap objects) and logs every hit.
void ScanRangeForTargets(const char* aLabel, uintptr_t aAddress, size_t aSize)
{
    std::array<uintptr_t, 64> pointers{};
    const size_t pointerCount = APM::Pose::TargetRegistry::GetProbePointers(pointers);
    if (pointerCount == 0)
    {
        return;
    }

    std::array<uint64_t, ProbeScanRange / 8> level2{};
    int hits = 0;
    for (size_t offset = 0; offset + 8 <= aSize && hits < 16; offset += 8)
    {
        uint64_t value = 0;
        if (!APM::SafeMemory::ReadU64(aAddress + offset, value))
        {
            continue;
        }
        if (IsProbePointer(value, pointers, pointerCount))
        {
            APM::Log::Info("Candidate:   %s+0x%zX -> %p (TARGET)", aLabel, offset, reinterpret_cast<void*>(value));
            ++hits;
            continue;
        }
        if (!APM::SafeMemory::LooksLikePointer(value) || APM::SafeMemory::IsInGameImage(value))
        {
            continue;
        }
        const size_t count2 = ReadBlock(value, level2);
        for (size_t j = 0; j < count2; ++j)
        {
            if (IsProbePointer(level2[j], pointers, pointerCount))
            {
                APM::Log::Info("Candidate:   %s+0x%zX -> %s -> +0x%zX -> %p (TARGET)", aLabel, offset,
                               DescribeValue(value).c_str(), j * 8, reinterpret_cast<void*>(level2[j]));
                ++hits;
                break;
            }
        }
    }
}

// A sample whose pose has the same bone count as one of V's rigs is a strong candidate.
// Dump everything that can help to find the sample -> entity mapping offline.
void DumpCandidate(void* aNode, uintptr_t aContext, const SampleOutput* aOut, uint32_t aBoneCount,
                   uintptr_t aReturnAddress, uintptr_t aStackPointer)
{
    if (g_candidateDumps.fetch_add(1) >= MaxCandidateDumps)
    {
        return;
    }

    const auto base = APM::SafeMemory::GetGameImageBase();
    uint64_t nodeVtbl = 0;
    APM::SafeMemory::ReadU64(reinterpret_cast<uintptr_t>(aNode), nodeVtbl);
    APM::Log::Info("Candidate: bones=%u ctx=%p node=%p nodeVtbl=+0x%llX pose=%p caller=+0x%llX",
                   aBoneCount, reinterpret_cast<void*>(aContext), aNode,
                   static_cast<unsigned long long>(nodeVtbl - base), reinterpret_cast<void*>(aOut->pose),
                   static_cast<unsigned long long>(aReturnAddress - base));

    // Call stack (game image frames only)
    void* frames[24]{};
    const USHORT frameCount = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    std::string stack;
    char chunk[32]{};
    for (USHORT i = 0; i < frameCount; ++i)
    {
        const auto address = reinterpret_cast<uintptr_t>(frames[i]);
        if (APM::SafeMemory::IsInGameImage(address))
        {
            std::snprintf(chunk, sizeof(chunk), " +0x%llX", static_cast<unsigned long long>(address - base));
            stack += chunk;
        }
    }
    APM::Log::Info("Candidate:   stack:%s", stack.c_str());

    ScanRangeForTargets("ctx", aContext, 0x200);
    ScanRangeForTargets("pose", aOut->pose, 0x100);
    ScanRangeForTargets("stack", aStackPointer, 0x1000);
}

void LogStatsPeriodically()
{
    const uint64_t now = GetTickCount64();
    uint64_t last = g_statLastLogTick.load();
    if (now - last < 3000 || !g_statLastLogTick.compare_exchange_strong(last, now))
    {
        return;
    }
    APM::Log::Verbose("Stats: sampleCalls=%llu outermost=%llu contextsScanned=%llu candidates=%llu mapping=%s boneCounts=[%s]",
                   static_cast<unsigned long long>(g_statAllCalls.load()),
                   static_cast<unsigned long long>(g_statOuterCalls.load()),
                   static_cast<unsigned long long>(g_statContexts.load()),
                   static_cast<unsigned long long>(g_statCandidates.load()),
                   g_contextPath.load() == PathUnknown ? "unknown" : "known", BoneCountsSeen().c_str());
    APM::Log::Verbose("Stats: mappingHits=%u mappingMisses=%u", g_pathHits.load(), g_pathMisses.load());
}

void OnPoseSampled(void* aNode, void* aContext, SampleOutput* aOut, uintptr_t aReturnAddress, uintptr_t aStackPointer)
{
    if (!aOut || !aOut->pose || APM::Pose::TargetRegistry::Count() == 0)
    {
        return;
    }

    const auto context = reinterpret_cast<uintptr_t>(aContext);
    const uint32_t path = g_contextPath.load(std::memory_order_acquire);

    if (path == PathUnknown)
    {
        // Probe phase. Contexts are pooled per worker, so every sample is inspected:
        // only samples whose bone count matches one of V's rigs are analysed in depth.
        uint32_t boneCount = 0;
        if (!APM::SafeMemory::ReadU32(aOut->pose + PoseBoneCount, boneCount))
        {
            return;
        }
        RecordBoneCount(boneCount);

        if (!APM::Pose::TargetRegistry::HasBoneCount(boneCount))
        {
            return;
        }
        g_statCandidates.fetch_add(1, std::memory_order_relaxed);

        uint32_t budget = g_probeBudget.load();
        while (budget > 0 && !g_probeBudget.compare_exchange_weak(budget, budget - 1))
        {
        }
        if (budget == 0)
        {
            return;
        }
        g_statContexts.fetch_add(1, std::memory_order_relaxed);

        DumpCandidate(aNode, context, aOut, boneCount, aReturnAddress, aStackPointer);
        ProbeContext(context);
        return;
    }

    const uintptr_t targetPointer = ResolveTargetFromContext(context, path);
    if (!APM::Pose::TargetRegistry::ContainsPointer(targetPointer))
    {
        // Validate the mapping: samples with V's bone counts must resolve to V at least sometimes.
        if (g_pathHits.load(std::memory_order_relaxed) == 0)
        {
            uint32_t boneCount = 0;
            if (APM::SafeMemory::ReadU32(aOut->pose + PoseBoneCount, boneCount) &&
                APM::Pose::TargetRegistry::HasBoneCount(boneCount) &&
                g_pathMisses.fetch_add(1) + 1 == PathValidationMisses)
            {
                APM::Log::Warn("Mapping ctx[+0x%X][+0x%X] does not resolve to any target, falling back to the probe",
                               path >> 16, path & 0xFFFF);
                g_contextPath = PathUnknown;
            }
        }
        return;
    }
    g_pathHits.fetch_add(1, std::memory_order_relaxed);

    if (!g_poseInfoLogged.load(std::memory_order_relaxed))
    {
        APM::Pose::Target target;
        if (APM::Pose::TargetRegistry::FindByPointer(targetPointer, target))
        {
            LogPoseInfo(aNode, context, aOut, target);
        }
    }

    ApplyOverrides(aNode, aOut, targetPointer);
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

    OnPoseSampled(aNode, aContext, aOut, reinterpret_cast<uintptr_t>(_ReturnAddress()),
                  reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));
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


    g_poseInfoLogged = false;
    g_countMismatchLogged = false;
    g_probeBudget = config.probeEnabled ? config.probeScanBudget : 0;
    g_statAllCalls = 0;
    g_statOuterCalls = 0;
    g_statContexts = 0;
    g_statCandidates = 0;
    g_candidateDumps = 0;
    g_pathHits = 0;
    g_pathMisses = 0;
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
