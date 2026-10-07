#include "Pose/GizmoState.hpp"

#include "Core/Log.hpp"

#include <RED4ext/CName.hpp>

#include <Windows.h>

#include <algorithm>
#include <mutex>
#include <vector>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;

constexpr size_t MaxSources = 8;
constexpr uint64_t StatsWindowMs = 1000;
constexpr uint64_t LogIntervalMs = 5000;

struct Source
{
    uintptr_t key = 0;
    std::vector<Vec3> positions; // model space
    std::vector<Quat> rotations; // model space
    uint64_t lastUpdate = 0;
    uint32_t samplesInWindow = 0;
    uint32_t samplesLastWindow = 0;
};

std::mutex g_mutex;
uintptr_t g_entity = 0;
uintptr_t g_component = 0;
std::vector<uint64_t> g_boneNames;
std::vector<Source> g_sources;
uint64_t g_windowStart = 0;
uint64_t g_lastLog = 0;

Quat GetRotation(const RED4ext::QsTransform& aTransform)
{
    return {aTransform.Rotation.i, aTransform.Rotation.j, aTransform.Rotation.k, aTransform.Rotation.r};
}

int32_t IndexOfLocked(uint64_t aBoneHash)
{
    const auto it = std::find(g_boneNames.begin(), g_boneNames.end(), aBoneHash);
    return it == g_boneNames.end() ? -1 : static_cast<int32_t>(std::distance(g_boneNames.begin(), it));
}

// The source the gizmos follow: the one with the most samples in the last full window. A tie keeps the current
// choice, so the markers never alternate between two outputs sampled equally often.
uintptr_t g_activeKey = 0;

const Source* ActiveSourceLocked()
{
    const Source* current = nullptr;
    const Source* best = nullptr;
    for (const auto& source : g_sources)
    {
        if (source.key == g_activeKey)
        {
            current = &source;
        }
        if (!best || source.samplesLastWindow > best->samplesLastWindow)
        {
            best = &source;
        }
    }
    if (current && best && best->samplesLastWindow <= current->samplesLastWindow)
    {
        return current;
    }
    if (best)
    {
        g_activeKey = best->key;
    }
    return best;
}

void LogSourcesLocked()
{
    const auto leftHand = IndexOfLocked(RED4ext::CName("LeftHand").hash);
    const auto hips = IndexOfLocked(RED4ext::CName("Hips").hash);
    const auto* active = ActiveSourceLocked();
    APM::Log::Info("Gizmo: %zu pose source(s) for V's main skeleton", g_sources.size());
    for (const auto& source : g_sources)
    {
        Vec3 hand{};
        Vec3 hip{};
        if (leftHand >= 0 && static_cast<size_t>(leftHand) < source.positions.size())
        {
            hand = source.positions[leftHand];
        }
        if (hips >= 0 && static_cast<size_t>(hips) < source.positions.size())
        {
            hip = source.positions[hips];
        }
        APM::Log::Info("Gizmo:   source %p%s samples/s=%u hips=(%.2f %.2f %.2f) leftHand=(%.2f %.2f %.2f)",
                       reinterpret_cast<void*>(source.key), &source == active ? " [active]" : "",
                       source.samplesLastWindow, hip.x, hip.y, hip.z, hand.x, hand.y, hand.z);
    }
}
} // namespace

namespace APM::Pose::GizmoState
{
void Publish(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount, uintptr_t aSource)
{
    if (aTarget.photoModeSlot != 0 || aTarget.debugName != "root" || aCount != aTarget.boneNames.size())
    {
        return;
    }

    thread_local std::vector<Vec3> positions;
    thread_local std::vector<Quat> rotations;
    positions.resize(aCount);
    rotations.resize(aCount);
    for (uint32_t i = 0; i < aCount; ++i)
    {
        const Vec3 localPos{aBones[i].Translation.X, aBones[i].Translation.Y, aBones[i].Translation.Z};
        const Quat localRot = GetRotation(aBones[i]);
        const int16_t parent = i < aTarget.parents.size() ? aTarget.parents[i] : int16_t(-1);
        if (parent >= 0 && static_cast<uint32_t>(parent) < i)
        {
            positions[i] = positions[parent] + Math::Rotate(rotations[parent], localPos);
            rotations[i] = Math::Normalize(Math::Mul(rotations[parent], localRot));
        }
        else
        {
            positions[i] = localPos;
            rotations[i] = localRot;
        }
    }

    const uint64_t now = GetTickCount64();
    std::scoped_lock lock(g_mutex);
    g_entity = aTarget.entity;
    g_component = aTarget.animatedComponent;
    if (g_boneNames != aTarget.boneNames)
    {
        g_boneNames = aTarget.boneNames;
        g_sources.clear();
    }

    auto it = std::find_if(g_sources.begin(), g_sources.end(), [&](const Source& s) { return s.key == aSource; });
    if (it == g_sources.end())
    {
        if (g_sources.size() >= MaxSources)
        {
            // Forget the stalest source.
            it = std::min_element(g_sources.begin(), g_sources.end(),
                                  [](const Source& a, const Source& b) { return a.lastUpdate < b.lastUpdate; });
            *it = Source{};
        }
        else
        {
            g_sources.emplace_back();
            it = std::prev(g_sources.end());
        }
        it->key = aSource;
    }
    it->positions = positions;
    it->rotations = rotations;
    it->lastUpdate = now;
    ++it->samplesInWindow;

    if (now - g_windowStart >= StatsWindowMs)
    {
        g_windowStart = now;
        // Sources not seen for a while are dropped.
        g_sources.erase(std::remove_if(g_sources.begin(), g_sources.end(),
                                       [&](const Source& s) { return now - s.lastUpdate > 3 * StatsWindowMs; }),
                        g_sources.end());
        for (auto& source : g_sources)
        {
            source.samplesLastWindow = source.samplesInWindow;
            source.samplesInWindow = 0;
        }
        if (now - g_lastLog >= LogIntervalMs)
        {
            g_lastLog = now;
            LogSourcesLocked();
        }
    }
}

void Clear()
{
    std::scoped_lock lock(g_mutex);
    g_entity = 0;
    g_component = 0;
    g_boneNames.clear();
    g_sources.clear();
    g_activeKey = 0;
}

bool GetBoneModelPosition(uint64_t aBoneHash, Math::Vec3& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto* source = ActiveSourceLocked();
    const auto index = IndexOfLocked(aBoneHash);
    if (!source || index < 0 || static_cast<size_t>(index) >= source->positions.size())
    {
        return false;
    }
    aOut = source->positions[index];
    return true;
}

bool GetBoneModelRotation(uint64_t aBoneHash, Math::Quat& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto* source = ActiveSourceLocked();
    const auto index = IndexOfLocked(aBoneHash);
    if (!source || index < 0 || static_cast<size_t>(index) >= source->rotations.size())
    {
        return false;
    }
    aOut = source->rotations[index];
    return true;
}

uintptr_t GetEntity()
{
    std::scoped_lock lock(g_mutex);
    return g_entity;
}

uintptr_t GetComponent()
{
    std::scoped_lock lock(g_mutex);
    return g_component;
}
} // namespace APM::Pose::GizmoState
