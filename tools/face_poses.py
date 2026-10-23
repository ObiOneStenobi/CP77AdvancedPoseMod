"""Builds the face pose data of the expression editor (dist/red4ext/plugins/AdvancedPoseMod/face/*.json).

Usage: python tools/face_poses.py <head>_rigsetup.facialsetup.json <head>_skeleton.rig.json <out.json> [--game 2.5]

The inputs are the WolvenKit JSON of a head facial setup and of its face_rig (tools/extract_face_poses.ps1 extracts them).
The main poses of the face and tongue (bakedData.<part>.AllMainPoses -> rig.trackNames, NumInbetweens; weights in
AllMainPosesInbetweens) point, in the same order, to mainPosesData.<part>.Poses -> Transforms {Rotation, Translation, Bone}.
The rest rotations of the face_rig are the identity, so a pose transform is the local rotation of the bone at full weight and
its translation is added to the rest translation.

Output: {"version", "head", "game", "bones": [names], "poses": [{"name", "zone", "side", "mirror",
"inbetweens": [{"w", "t": [[bone, qx, qy, qz, qw, tx, ty, tz], ...]}]}]} (bone = index in "bones", translation in meters).
Skipped: gaze, pupils, neck, head, face gravity, sculpt, sticky lips, scale poses, empty poses and correctives.
"""
import json
import math
import re
import sys

PARTS = ("Face", "Tongue")
SKIP = re.compile(r"^(neck_|head_|face_gravity_|sculp_)|_dir_|_pupil_|sticky")
ZONES = (("brows", re.compile(r"^eye_[lr]_brows_")), ("eyelids", re.compile(r"^eye_")), ("nose", re.compile(r"^nose_")),
         ("cheeks", re.compile(r"^cheek_")), ("lips", re.compile(r"^lips_")), ("jaw", re.compile(r"^jaw_")),
         ("ears", re.compile(r"^ear_")), ("tongue", re.compile(r"^tongue_")))
MIN_ANGLE = math.radians(0.01)  # smaller rotations / moves are dropped
MIN_MOVE = 1e-6


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)["Data"]["RootChunk"]


def zone_of(name):
    for zone, pattern in ZONES:
        if pattern.search(name):
            return zone
    return None


def side_of(name):
    m = re.match(r"^[a-z]+_([lr])_", name)
    return m.group(1) if m else ""


def mirror_of(name, side):
    if not side:
        return ""
    other = "r" if side == "l" else "l"
    return re.sub(r"^([a-z]+)_[lr]_", lambda m: f"{m.group(1)}_{other}_", name, count=1)


def transform(t):
    q = t["Rotation"]
    qx, qy, qz, qw = q["i"], q["j"], q["k"], q["r"]
    n = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
    qx, qy, qz, qw = (v / n for v in (qx, qy, qz, qw))
    if qw < 0:
        qx, qy, qz, qw = -qx, -qy, -qz, -qw
    p = t["Translation"]
    return [qx, qy, qz, qw, p["X"], p["Y"], p["Z"]]


def significant(v):
    angle = 2 * math.acos(min(1.0, v[3]))
    return angle >= MIN_ANGLE or math.dist(v[4:], (0, 0, 0)) >= MIN_MOVE


def build(setup, rig, game):
    tracks = [x["$value"] for x in rig["trackNames"]]
    rig_bones = [x["$value"] for x in rig["boneNames"]]
    bones, bone_index = [], {}
    poses, skipped = [], []
    for part in PARTS:
        baked = setup["bakedData"]["Data"][part]
        data = setup["mainPosesData"]["Data"][part]
        weights = baked["AllMainPosesInbetweens"]
        idx = 0
        for main in baked["AllMainPoses"]:
            name = tracks[main["Track"]]
            entries = list(range(idx, idx + main["NumInbetweens"]))
            idx += main["NumInbetweens"]
            zone = zone_of(name)
            if SKIP.search(name) or zone is None:
                skipped.append(name)
                continue
            if any(data["Poses"][i]["IsScale"] for i in entries):
                skipped.append(name + " (scale)")
                continue
            inbetweens = []
            for i in entries:
                pose = data["Poses"][i]
                rows = []
                for t in data["Transforms"][pose["TransformIdx"]: pose["TransformIdx"] + pose["NumTransforms"]]:
                    v = transform(t)
                    if not significant(v):
                        continue
                    bone = rig_bones[t["Bone"]]
                    if bone not in bone_index:
                        bone_index[bone] = len(bones)
                        bones.append(bone)
                    rows.append([bone_index[bone]] + [round(x, 6) for x in v])
                inbetweens.append({"w": round(weights[i], 6), "t": rows})
            if not any(ib["t"] for ib in inbetweens):
                skipped.append(name + " (empty)")
                continue
            side = side_of(name)
            poses.append({"name": name, "zone": zone, "side": side, "mirror": mirror_of(name, side),
                          "inbetweens": inbetweens})
    names = {p["name"] for p in poses}
    for p in poses:
        if p["mirror"] not in names:
            p["mirror"] = ""
    head = setup["rig"]["DepotPath"]["$value"].replace("\\", "/").split("/")[-1]
    head = head.replace("_skeleton.rig", "")
    return {"version": 1, "head": head, "game": game, "bones": bones, "poses": poses}, skipped


def write(path, data):
    # one pose per line: small and readable in a diff
    head = {k: v for k, v in data.items() if k != "poses"}
    text = json.dumps(head, separators=(",", ":"))[:-1] + ',"poses":[\n'
    text += ",\n".join(json.dumps(p, separators=(",", ":")) for p in data["poses"]) + "\n]}\n"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return len(text.encode("utf-8"))


def main(argv):
    game = "2.5"
    if "--game" in argv:
        i = argv.index("--game")
        game = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    if len(argv) != 3:
        sys.exit(__doc__)
    data, skipped = build(load(argv[0]), load(argv[1]), game)
    size = write(argv[2], data)
    zones = {}
    for p in data["poses"]:
        zones[p["zone"]] = zones.get(p["zone"], 0) + 1
    rows = sum(len(ib["t"]) for p in data["poses"] for ib in p["inbetweens"])
    print(f"{data['head']}: {len(data['poses'])} poses, {sum(len(p['inbetweens']) for p in data['poses'])} inbetweens, "
          f"{len(data['bones'])} bones, {rows} transforms, {size / 1024:.0f} KB")
    print("  zones: " + ", ".join(f"{z} {n}" for z, n in zones.items()))
    print(f"  skipped {len(skipped)}: " + ", ".join(skipped))


if __name__ == "__main__":
    main(sys.argv[1:])
