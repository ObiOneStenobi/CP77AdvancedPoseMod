#include "Game/AnimGraphHook.hpp"

#include "Core/Config.hpp"
#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Core/SafeMemory.hpp"
#include "Game/EngineLayout.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Math/Quat.hpp"
#include "Pose/EyelidFollow.hpp"
#include "Pose/FacePoses.hpp"
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
#include <cstdio>
#include <malloc.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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
constexpr uintptr_t PoseRigBoneCount = 0x00; // uint32, bones of the rig (the transforms can be more)
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

// Once per character and size: how many of the first rig bones have the reference bone length, to check that the
// extra transforms are after the rig's bones (an extra one in the middle would shift the bones after it).
void LogExtraTransforms(const APM::Pose::Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    static std::mutex s_mutex;
    static std::unordered_set<uint64_t> s_logged;
    {
        std::scoped_lock lock(s_mutex);
        if (!s_logged.insert((static_cast<uint64_t>(aTarget.photoModeSlot) << 32) | aCount).second)
        {
            return;
        }
    }
    const size_t rigCount = aTarget.boneNames.size();
    size_t matching = 0;
    size_t compared = 0;
    for (size_t i = 0; i < rigCount && i < aTarget.refTranslationsLS.size(); ++i)
    {
        const auto& ref = aTarget.refTranslationsLS[i];
        const float refLength = std::sqrt(ref.x * ref.x + ref.y * ref.y + ref.z * ref.z);
        if (refLength < 0.01f)
        {
            continue;
        }
        const auto& t = aBones[i].Translation;
        const float length = std::sqrt(t.X * t.X + t.Y * t.Y + t.Z * t.Z);
        ++compared;
        if (std::fabs(length - refLength) < 0.01f + refLength * 0.05f)
        {
            ++matching;
        }
    }
    APM::Log::Verbose("Character %u: pose has %u transforms for %zu rig bones on %s; using the first %zu (%zu of %zu "
                   "bones have the reference length)",
                   aTarget.photoModeSlot, aCount, rigCount, aTarget.debugName.c_str(), rigCount, matching, compared);
}

// Diagnostics ([Debug] Log=1): what each registered skeleton receives from the animation, per animated component,
// to tell why a character cannot be posed (never sampled, or sampled with another bone count).
struct SampleNote
{
    uint32_t samples = 0;
    uint32_t accepted = 0;     // samples with the rig's bone count (posed when there are edits)
    uint32_t lastCount = 0;    // transforms in the last pose
    uint32_t lastRigCount = 0; // rig bone count in the last pose header
};
std::mutex g_sampleNotesMutex;
std::unordered_map<uintptr_t, SampleNote> g_sampleNotes;

void NoteSample(const APM::Pose::Target& aTarget, uint32_t aCount, uint32_t aRigCount, bool aAccepted)
{
    std::scoped_lock lock(g_sampleNotesMutex);
    auto& note = g_sampleNotes[aTarget.animatedComponent];
    ++note.samples;
    note.accepted += aAccepted ? 1 : 0;
    note.lastCount = aCount;
    note.lastRigCount = aRigCount;
}

void ApplyOverrides(void* aNode, uintptr_t aContext, const SampleOutput* aOut, uintptr_t aTargetPointer)
{
    uint32_t boneCount = 0;
    uint64_t bonesAddress = 0;
    if (!APM::SafeMemory::ReadU32(aOut->pose + PoseBoneCount, boneCount) ||
        !APM::SafeMemory::ReadU64(aOut->pose + PoseBones, bonesAddress) || boneCount == 0 || boneCount > MaxBones)
    {
        return;
    }

    APM::Pose::TargetRegistry::VisitTarget(aTargetPointer, [&](const APM::Pose::Target& aTarget) {
        // Each character (V, added characters) has its own pose.
        const auto snapshot = APM::Pose::PoseOverrides::GetSnapshot(aTarget.photoModeSlot);
        const bool applyPose = APM::Pose::PoseOverrides::IsEnabled(aTarget.photoModeSlot) && !snapshot->IsEmpty();
        uint32_t rigBoneCount = 0;
        if (APM::Log::g_verbose.load(std::memory_order_relaxed))
        {
            uint32_t headerRigCount = 0;
            APM::SafeMemory::ReadU32(aOut->pose + PoseRigBoneCount, headerRigCount);
            const bool accepted = boneCount == aTarget.boneNames.size() ||
                                  (boneCount > aTarget.boneNames.size() && headerRigCount == aTarget.boneNames.size());
            NoteSample(aTarget, boneCount, headerRigCount, accepted);
        }
        if (boneCount > aTarget.boneNames.size() && APM::SafeMemory::ReadU32(aOut->pose + PoseRigBoneCount, rigBoneCount) &&
            rigBoneCount == aTarget.boneNames.size())
        {
            // Added characters' graphs append extra transforms after the rig's bones: only the rig's bones are used.
            LogExtraTransforms(aTarget, reinterpret_cast<const RED4ext::QsTransform*>(bonesAddress), boneCount);
            boneCount = rigBoneCount;
        }
        if (boneCount != aTarget.boneNames.size())
        {
            // Once per character and skeleton size (an animation LOD samples fewer bones than the rig has).
            static std::mutex s_mismatchMutex;
            static std::unordered_set<uint64_t> s_mismatchLogged;
            const uint64_t key = (static_cast<uint64_t>(aTarget.photoModeSlot) << 32) | boneCount;
            std::scoped_lock lock(s_mismatchMutex);
            if (s_mismatchLogged.insert(key).second)
            {
                APM::Log::Verbose("Character %u: pose bone count (%u) differs from rig bone count (%zu) on %s; skipped",
                               aTarget.photoModeSlot, boneCount, aTarget.boneNames.size(), aTarget.debugName.c_str());
            }
            return;
        }

        auto* bones = reinterpret_cast<RED4ext::QsTransform*>(bonesAddress);
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

// Diagnostics ([Debug] Log=1): body poses of a Photo Mode character whose context resolves to none of its registered
// skeletons (e.g. a body graph that another mod replaced), once per resolved pointer.
constexpr size_t MaxUnresolvedLogs = 48;
std::mutex g_unresolvedMutex;
std::unordered_set<uintptr_t> g_unresolvedLogged;

void NoteUnresolved(uintptr_t aContext, const SampleOutput* aOut, uintptr_t aResolved)
{
    uint32_t boneCount = 0;
    uint32_t rigCount = 0;
    if (!APM::SafeMemory::ReadU32(aOut->pose + PoseBoneCount, boneCount))
    {
        return;
    }
    APM::SafeMemory::ReadU32(aOut->pose + PoseRigBoneCount, rigCount);
    // Body skeletons only (V's and the human characters' main skeleton: 71 bones, 72 transforms for added ones).
    if (rigCount != 71 && boneCount != 71)
    {
        return;
    }
    // Only graphs of the Photo Mode characters (the world's NPCs have the same skeleton).
    uint64_t owner = 0;
    uint64_t entity = 0;
    if (APM::SafeMemory::ReadU64(aContext + 0x188, owner) && APM::SafeMemory::LooksLikePointer(owner))
    {
        APM::SafeMemory::ReadU64(owner + 0xB8, entity);
    }
    if (!APM::Pose::TargetRegistry::ContainsPointer(entity))
    {
        return;
    }
    {
        std::scoped_lock lock(g_unresolvedMutex);
        if (g_unresolvedLogged.size() >= MaxUnresolvedLogs || !g_unresolvedLogged.insert(aResolved).second)
        {
            return;
        }
    }
    // The resolved object's name (components: CName at +0x40).
    const char* name = nullptr;
    uint64_t vtbl = 0;
    uint64_t nameHash = 0;
    if (APM::SafeMemory::LooksLikePointer(aResolved) && APM::SafeMemory::ReadU64(aResolved, vtbl) &&
        APM::SafeMemory::IsInGameImage(vtbl) && APM::SafeMemory::ReadU64(aResolved + 0x40, nameHash) && nameHash)
    {
        name = RED4ext::CName(nameHash).ToString();
    }
    APM::Log::Verbose("Unresolved pose of character entity %p: %u transforms (rig %u) -> %s name=%s",
                      reinterpret_cast<void*>(entity), boneCount, rigCount, DescribeValue(aResolved).c_str(),
                      name ? name : "?");
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
        if (APM::Log::g_verbose.load(std::memory_order_relaxed))
        {
            NoteUnresolved(context, aOut, targetPointer);
        }
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

    ApplyOverrides(aNode, context, aOut, targetPointer);
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

// ---------------------------------------------------------------------------------------------------------------------
// Face tracks ([Face] GameSolver=1, see docs/RESEARCH.md "Face poses"). animAnimNode_Sermo is the game's facial
// solver: it samples its input (the face_rig pose with one float track per face pose, set by the Photo Mode expression)
// and turns the tracks into bones, correctives included. While it runs, its input node (+0x58, as for
// animAnimNode_Output) gets a copy of its vtable whose Sample is FaceInput_Sample: after the input wrote the tracks and
// before the solver reads them, the expression editor's weights go in (Pose/FacePoses). With [Probe] FaceTracks=1 the
// tracks are also logged and one is raised from the config.
// ---------------------------------------------------------------------------------------------------------------------
constexpr uintptr_t NodeInput = 0x58;      // input node of a one-pose-input node
constexpr uintptr_t PoseTrackCount = 0x28; // uint32
constexpr uintptr_t PoseTracks = 0x38;     // float*
constexpr uint32_t MaxTracks = 4096;
constexpr size_t FakeVtableSlots = 96;     // entries copied (Sample is slot 37)

void* g_addrSermoSample = nullptr;
Sample_t g_origSermoSample = nullptr;

struct FakeVtable
{
    uintptr_t original = 0;
    Sample_t originalSample = nullptr;
    std::array<uintptr_t, FakeVtableSlots> slots{};
};
std::mutex g_fakeVtablesMutex;
std::unordered_map<uintptr_t, std::unique_ptr<FakeVtable>> g_fakeVtables;   // by original vtable
std::unordered_map<uintptr_t, FakeVtable*> g_fakeVtablesByCopy;            // by copy (slots.data())

thread_local Sample_t t_faceInputSample = nullptr; // original Sample of the input node being sampled for Sermo
thread_local void* t_faceInputNode = nullptr;

std::atomic<bool> g_sermoFirstLogged{false};
std::atomic<bool> g_faceProbe{false}; // [Probe] FaceTracks
std::atomic<uint64_t> g_sermoCalls{0};

struct FaceTrackLog
{
    std::string last;   // non-zero tracks last logged
    uint64_t tick = 0;
    bool headerLogged = false;
    int32_t probeTrack = -2; // index of the config track in this rig (-1 missing, -2 not looked up)
    uint64_t probeTrackHash = 0;
};
std::mutex g_faceTrackLogMutex;
std::unordered_map<uintptr_t, FaceTrackLog> g_faceTrackLogs; // by animated component

void FaceInput_Sample(void* aNode, void* aContext, void* a3, SampleOutput* aOut);

// Vtable copy of an input node class, made once per class.
const FakeVtable* GetFakeVtable(uintptr_t aVtable)
{
    std::scoped_lock lock(g_fakeVtablesMutex);
    if (const auto copy = g_fakeVtablesByCopy.find(aVtable); copy != g_fakeVtablesByCopy.end())
    {
        return copy->second; // another thread is sampling the same node right now
    }
    if (const auto it = g_fakeVtables.find(aVtable); it != g_fakeVtables.end())
    {
        return it->second.get();
    }
    if (!APM::SafeMemory::IsInGameImage(aVtable))
    {
        return nullptr;
    }
    auto fake = std::make_unique<FakeVtable>();
    if (!APM::SafeMemory::ReadBytes(aVtable, fake->slots.data(), sizeof(uintptr_t) * FakeVtableSlots))
    {
        return nullptr;
    }
    fake->original = aVtable;
    fake->originalSample = reinterpret_cast<Sample_t>(fake->slots[SampleVtableSlot]);
    if (!APM::SafeMemory::IsInGameImage(reinterpret_cast<uintptr_t>(fake->originalSample)))
    {
        return nullptr;
    }
    fake->slots[SampleVtableSlot] = reinterpret_cast<uintptr_t>(&FaceInput_Sample);
    const auto base = APM::SafeMemory::GetGameImageBase();
    APM::Log::Info("Face: solver input vtable=+0x%llX Sample=+0x%llX", static_cast<unsigned long long>(aVtable - base),
                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(fake->originalSample) - base));
    auto* raw = fake.get();
    g_fakeVtablesByCopy[reinterpret_cast<uintptr_t>(raw->slots.data())] = raw;
    g_fakeVtables[aVtable] = std::move(fake);
    return raw;
}

// The face tracks of a posed character, as the solver is about to read them.
void OnFaceTracks(void* aContext, SampleOutput* aOut)
{
    const uint32_t path = g_contextPath.load(std::memory_order_acquire);
    if (!aOut || !aOut->pose || path == PathUnknown)
    {
        return;
    }
    const uintptr_t targetPointer = ResolveTargetFromContext(reinterpret_cast<uintptr_t>(aContext), path);
    if (!APM::Pose::TargetRegistry::ContainsPointer(targetPointer))
    {
        return;
    }
    uint32_t trackCount = 0;
    uint64_t tracksAddress = 0;
    if (!APM::SafeMemory::ReadU32(aOut->pose + PoseTrackCount, trackCount) ||
        !APM::SafeMemory::ReadU64(aOut->pose + PoseTracks, tracksAddress) || trackCount == 0 || trackCount > MaxTracks ||
        !APM::SafeMemory::LooksLikePointer(tracksAddress))
    {
        return;
    }
    auto* tracks = reinterpret_cast<float*>(tracksAddress);
    const bool probe = g_faceProbe.load(std::memory_order_relaxed);

    APM::Pose::TargetRegistry::VisitTarget(targetPointer, [&](const APM::Pose::Target& aTarget) {
        APM::EngineLayout::RigView rig;
        const bool hasRig = APM::EngineLayout::ViewRig(aTarget.rig, rig);
        const uint32_t named = hasRig ? std::min(rig.trackCount, trackCount) : 0;
        if (named > 0)
        {
            const auto snapshot = APM::Pose::PoseOverrides::GetSnapshot(aTarget.photoModeSlot);
            APM::Pose::FacePoses::ApplyTracks(aTarget, tracks, trackCount, rig.trackNames, named, *snapshot,
                                              APM::Pose::PoseOverrides::IsEnabled(aTarget.photoModeSlot));
        }
        if (!probe)
        {
            return;
        }
        const auto config = APM::Config::Get();
        const auto name = [&](uint32_t aIndex) -> std::string {
            const char* text = aIndex < named ? rig.trackNames[aIndex].ToString() : nullptr;
            return text ? std::string(text) : "#" + std::to_string(aIndex);
        };

        std::scoped_lock lock(g_faceTrackLogMutex);
        auto& log = g_faceTrackLogs[aTarget.animatedComponent];
        if (!log.headerLogged)
        {
            log.headerLogged = true;
            APM::Log::Info("FaceProbe: character %u %s: %u tracks in the pose, %u in the rig", aTarget.photoModeSlot,
                           aTarget.debugName.c_str(), trackCount, hasRig ? rig.trackCount : 0);
        }

        // The config track, raised before the solver reads it.
        const uint64_t wantedHash = config.probeFaceTrack.empty() ? 0 : RED4ext::CName(config.probeFaceTrack.c_str()).hash;
        if (log.probeTrackHash != wantedHash)
        {
            log.probeTrackHash = wantedHash;
            log.probeTrack = -2;
        }
        if (wantedHash != 0 && log.probeTrack == -2)
        {
            log.probeTrack = -1;
            for (uint32_t i = 0; i < named; ++i)
            {
                if (rig.trackNames[i].hash == wantedHash)
                {
                    log.probeTrack = static_cast<int32_t>(i);
                }
            }
            APM::Log::Info("FaceProbe: %s: track '%s' %s", aTarget.debugName.c_str(), config.probeFaceTrack.c_str(),
                           log.probeTrack >= 0 ? ("is #" + std::to_string(log.probeTrack)).c_str() : "not found");
        }
        if (log.probeTrack >= 0 && config.probeFaceTrackWeight != 0.f)
        {
            float& value = tracks[log.probeTrack];
            value = std::clamp(value + config.probeFaceTrackWeight, -1.f, 1.f);
        }

        // Non-zero tracks, logged when they change (at most twice a second).
        std::string text;
        uint32_t nonZero = 0;
        for (uint32_t i = 0; i < trackCount; ++i)
        {
            const float value = tracks[i];
            if (!std::isfinite(value) || std::abs(value) < 0.01f)
            {
                continue;
            }
            ++nonZero;
            if (nonZero <= 60)
            {
                char number[16]{};
                std::snprintf(number, sizeof(number), "%.2f", value);
                text += " " + name(i) + "=" + number;
            }
        }
        const uint64_t now = GetTickCount64();
        if (text != log.last && now - log.tick >= 500)
        {
            log.last = text;
            log.tick = now;
            APM::Log::Info("FaceProbe: character %u %s: %u non-zero:%s%s", aTarget.photoModeSlot,
                           aTarget.debugName.c_str(), nonZero, text.c_str(), nonZero > 60 ? " ..." : "");
        }
    });
}

void FaceInput_Sample(void* aNode, void* aContext, void* a3, SampleOutput* aOut)
{
    Sample_t original = t_faceInputSample;
    const bool forSermo = original && aNode == t_faceInputNode;
    if (!original || !forSermo)
    {
        // Sampled outside the solver while another thread had the copy in place: the class's own Sample.
        uint64_t vtable = 0;
        original = nullptr;
        if (APM::SafeMemory::ReadU64(reinterpret_cast<uintptr_t>(aNode), vtable))
        {
            if (const auto* fake = GetFakeVtable(vtable))
            {
                original = fake->originalSample;
            }
        }
        if (!original)
        {
            return;
        }
        original(aNode, aContext, a3, aOut);
        return;
    }
    t_faceInputSample = nullptr; // nested samples of other nodes are not ours
    original(aNode, aContext, a3, aOut);
    OnFaceTracks(aContext, aOut);
}

void Detour_SermoSample(void* aNode, void* aContext, void* a3, SampleOutput* aOut)
{
    uint64_t input = 0;
    uint64_t vtable = 0;
    if (!APM::PhotoModeTracker::IsActive() || g_contextPath.load(std::memory_order_relaxed) == PathUnknown ||
        !APM::SafeMemory::ReadU64(reinterpret_cast<uintptr_t>(aNode) + NodeInput, input) ||
        !APM::SafeMemory::LooksLikePointer(input) || !APM::SafeMemory::ReadU64(input, vtable))
    {
        g_origSermoSample(aNode, aContext, a3, aOut);
        return;
    }
    const auto* fake = GetFakeVtable(vtable);
    if (!fake)
    {
        g_origSermoSample(aNode, aContext, a3, aOut);
        return;
    }
    if (!g_sermoFirstLogged.exchange(true))
    {
        APM::Log::Info("Face: first solver sample (node %p, input %p)", aNode, reinterpret_cast<void*>(input));
    }
    g_sermoCalls.fetch_add(1, std::memory_order_relaxed);

    auto* object = reinterpret_cast<volatile LONG64*>(input);
    const auto copy = static_cast<LONG64>(reinterpret_cast<uintptr_t>(fake->slots.data()));
    const auto original = static_cast<LONG64>(fake->original);
    const Sample_t previousSample = t_faceInputSample;
    void* const previousNode = t_faceInputNode;
    t_faceInputSample = fake->originalSample;
    t_faceInputNode = reinterpret_cast<void*>(input);
    InterlockedExchange64(object, copy);
    g_origSermoSample(aNode, aContext, a3, aOut);
    InterlockedCompareExchange64(object, original, copy);
    t_faceInputSample = previousSample;
    t_faceInputNode = previousNode;
}

uintptr_t ResolveNodeVtable(const char* aClass)
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* cls = rtti ? rtti->GetClass(aClass) : nullptr;
    if (!cls)
    {
        APM::Log::Error("AnimGraph: %s class not found", aClass);
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
    const uintptr_t vtable = ResolveNodeVtable("animAnimNode_Output");
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

    // The game's facial solver: the expression editor's weights go into its face tracks.
    const auto config = Config::Get();
    g_faceProbe = config.probeFaceTracks;
    APM::Pose::EyelidFollow::SetProbe(config.probeFaceTracks);
    if (config.faceGameSolver || config.probeFaceTracks)
    {
        const uintptr_t sermoVtable = ResolveNodeVtable("animAnimNode_Sermo");
        const uintptr_t sermoSample =
            SafeMemory::IsInGameImage(sermoVtable) ? reinterpret_cast<uintptr_t*>(sermoVtable)[SampleVtableSlot] : 0;
        // Expected start: the input node from +0x58 (mov rcx, [rcx+58h] after the register saves).
        constexpr std::array<uint8_t, 4> InputLoad = {0x48, 0x8B, 0x49, 0x58};
        bool loadsInput = false;
        for (size_t i = 0; SafeMemory::IsInGameImage(sermoSample) && i + InputLoad.size() <= 64 && !loadsInput; ++i)
        {
            loadsInput = std::memcmp(reinterpret_cast<const void*>(sermoSample + i), InputLoad.data(), InputLoad.size()) == 0;
        }
        if (SafeMemory::IsInGameImage(sermoSample) && loadsInput)
        {
            Log::Info("Face: animAnimNode_Sermo vtable=+0x%llX Sample=+0x%llX", static_cast<unsigned long long>(sermoVtable - base),
                      static_cast<unsigned long long>(sermoSample - base));
            g_addrSermoSample = reinterpret_cast<void*>(sermoSample);
            if (!AttachHook(g_addrSermoSample, &Detour_SermoSample, &g_origSermoSample))
            {
                Log::Error("Face: failed to attach the solver hook (face poses on the bones, no correctives)");
                g_addrSermoSample = nullptr;
            }
        }
        else
        {
            Log::Warn("Face: facial solver not recognized (face poses on the bones, no correctives); bytes:%s",
                      SafeMemory::IsInGameImage(sermoSample) ? HexDump(sermoSample, 64).c_str() : " none");
        }
    }
    return true;
}

void Uninstall()
{
    if (g_addrSermoSample)
    {
        DetachHook(g_addrSermoSample);
        g_addrSermoSample = nullptr;
    }
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
    {
        std::scoped_lock lock(g_faceTrackLogMutex);
        g_faceTrackLogs.clear(); // log every character's tracks again
    }
    {
        std::scoped_lock lock(g_sampleNotesMutex);
        g_sampleNotes.clear();
    }
    {
        std::scoped_lock lock(g_unresolvedMutex);
        g_unresolvedLogged.clear();
    }
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

void LogCharacterSkeletons(uint32_t aCharacter)
{
    if (!Log::g_verbose.load(std::memory_order_relaxed))
    {
        return;
    }
    struct Line
    {
        std::string name;
        size_t bones = 0;
        uintptr_t component = 0;
        uintptr_t entity = 0;
    };
    std::vector<Line> lines;
    Pose::TargetRegistry::ForEach([&](const Pose::Target& aTarget) {
        if (aTarget.photoModeSlot == aCharacter)
        {
            lines.push_back({aTarget.debugName, aTarget.boneNames.size(), aTarget.animatedComponent, aTarget.entity});
        }
    });
    if (lines.empty())
    {
        Log::Verbose("Character %u skeletons: none registered", aCharacter);
        return;
    }
    Log::Verbose("Character %u skeletons (entity %p), samples since Photo Mode opened:", aCharacter,
                 reinterpret_cast<void*>(lines.front().entity));
    std::scoped_lock lock(g_sampleNotesMutex);
    for (const auto& line : lines)
    {
        const auto it = g_sampleNotes.find(line.component);
        if (it == g_sampleNotes.end())
        {
            Log::Verbose("  %s (%zu bones, component %p): never sampled", line.name.c_str(), line.bones,
                         reinterpret_cast<void*>(line.component));
            continue;
        }
        const auto& note = it->second;
        Log::Verbose("  %s (%zu bones, component %p): %u samples, %u with the rig's bone count; last %u transforms, "
                     "header rig count %u",
                     line.name.c_str(), line.bones, reinterpret_cast<void*>(line.component), note.samples,
                     note.accepted, note.lastCount, note.lastRigCount);
    }
}
} // namespace APM::AnimGraphHook
