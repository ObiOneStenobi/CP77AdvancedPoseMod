// AdvancedPoseMod - native functions implemented in red4ext/plugins/AdvancedPoseMod/AdvancedPoseMod.dll
//
// Rotations are in degrees around the bone's local axes (applied Z * Y * X), on top of the current pose.
// Translations are in meters, in the parent bone's space, on top of the current pose.
// Bone names: see probe/rig_*.txt (V's main skeleton is the "root" component, 71 bones).

public native func APM_IsPhotoModeActive() -> Bool
public native func APM_GetBoneCount() -> Int32
public native func APM_GetBoneName(index: Int32) -> CName

public native func APM_SetBoneRotation(bone: CName, x: Float, y: Float, z: Float) -> Bool
public native func APM_SetBoneTranslation(bone: CName, x: Float, y: Float, z: Float) -> Bool
// xyz = value, w = 1 if the bone currently has this override
public native func APM_GetBoneRotation(bone: CName) -> Vector4
public native func APM_GetBoneTranslation(bone: CName) -> Vector4
public native func APM_ResetBone(bone: CName) -> Void
public native func APM_ResetAll() -> Void

// Master switch: editing a bone or loading a pose turns it on, leaving Photo Mode turns it off.
public native func APM_SetEnabled(enabled: Bool) -> Void
public native func APM_IsEnabled() -> Bool

// Pose files: red4ext/plugins/AdvancedPoseMod/poses/<name>.json
public native func APM_SavePose(name: String) -> Bool
public native func APM_LoadPose(name: String) -> Bool
// Refreshes the list of saved poses and returns its size
public native func APM_GetPoseCount() -> Int32
public native func APM_GetPoseName(index: Int32) -> String
