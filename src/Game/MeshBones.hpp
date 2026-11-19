#pragma once

#include <cstdint>
#include <unordered_set>

// Bones the visible meshes of a character are skinned to (CMesh::boneNames of every mesh component that shows
// something). Physics skeletons often carry bones no mesh uses (generic clothing rigs, leftovers): those are hidden.
// Game thread only: reads the meshes the components already loaded.
namespace APM::MeshBones
{
// aComplete is false when a visible mesh is not loaded yet (or cannot be read): then the set is partial.
// Returns false when the entity cannot be read.
bool Collect(uintptr_t aEntity, std::unordered_set<uint64_t>& aOut, bool& aComplete);
} // namespace APM::MeshBones
