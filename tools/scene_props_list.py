"""Builds the Scene tab list (scene_props.txt) from the game's own .ent templates, without characters.

Input: a folder with the .ent files extracted from the game archives and serialized to JSON next to them
(WolvenKit.CLI unbundle -w "*.ent", then convert serialize on the same folder), see tools\\scene_props_list.ps1.
A template is left out when:
  - its path is under a "characters\\" folder or the file is "player_*" (same rule as SceneProps::IsAllowed);
  - the entity is a puppet (NPCPuppet, PlayerPuppet, gamePuppet...): NPCs placed by quests and the open world;
  - an appearance comes from a "characters\\" folder, an animated component uses a character rig or a skinned mesh
    is a character's (bodies made of plain entities);
  - it is an instanced crowd (entInstancedAnimationComponent: stadium and street crowds);
  - it could not be read.
Usage: python -I scene_props_list.py <extracted folder> <output scene_props.txt> [report.txt]
"""

import json
import os
import sys
from collections import Counter


def is_character_path(lower):
    file = lower.rsplit("\\", 1)[-1]
    return "characters\\" in lower or file.startswith("player_")


def depot(value):
    try:
        return value["DepotPath"]["$value"].lower()
    except (KeyError, TypeError):
        return ""


def classify(path):
    """Returns (keep, reason, root type)."""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        root = data["Data"]["RootChunk"]
        chunks = root["compiledData"]["Data"].get("Chunks", [])
    except Exception:  # noqa: BLE001 - any unreadable template is left out
        return False, "unreadable", ""
    kind = chunks[0].get("$type", "") if chunks else ""
    if "Puppet" in kind:
        return False, "puppet", kind
    for appearance in root.get("appearances", []) or []:
        if "\\characters\\" in "\\" + depot(appearance.get("appearanceResource")):
            return False, "character appearance", kind
    for chunk in chunks:
        ctype = chunk.get("$type", "")
        if ctype == "entInstancedAnimationComponent":
            return False, "crowd", kind
        if ctype == "entAnimatedComponent" and "\\characters\\" in "\\" + depot(chunk.get("rig")):
            return False, "character rig", kind
        if "SkinnedMeshComponent" in ctype and "\\characters\\" in "\\" + depot(chunk.get("mesh")):
            return False, "character mesh", kind
    return True, "", kind


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    source, output = sys.argv[1], sys.argv[2]
    report = sys.argv[3] if len(sys.argv) > 3 else None
    kept, reasons, kinds, left = [], Counter(), Counter(), []
    for folder, _, files in os.walk(source):
        for name in files:
            if not name.lower().endswith(".ent"):
                continue
            full = os.path.join(folder, name)
            rel = os.path.relpath(full, source)
            lower = rel.lower()
            if is_character_path(lower):
                reasons["characters folder"] += 1
                continue
            json_path = full + ".json"
            if not os.path.isfile(json_path):
                reasons["unreadable"] += 1
                left.append(f"unreadable\t{rel}")
                continue
            keep, reason, kind = classify(json_path)
            if keep:
                kept.append(rel)
                kinds[kind] += 1
            else:
                reasons[reason] += 1
                left.append(f"{reason}\t{kind}\t{rel}")
    kept.sort(key=str.lower)
    with open(output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(kept) + "\n")
    print(f"kept {len(kept)}, left out {sum(reasons.values())}: " +
          ", ".join(f"{k} {v}" for k, v in reasons.most_common()))
    if report:
        with open(report, "w", encoding="utf-8", newline="\n") as f:
            f.write("# kept by entity type\n")
            f.writelines(f"{v}\t{k}\n" for k, v in kinds.most_common())
            f.write("# left out\n")
            f.writelines(line + "\n" for line in sorted(left, key=str.lower))
    return 0


if __name__ == "__main__":
    sys.exit(main())
