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

// IK chains: n"LeftHand", n"RightHand", n"LeftFoot", n"RightFoot".
// Enabled = the hand/foot is pinned where the original pose put it, plus an offset in meters
// (model space: X right, Y forward, Z up). swivel (degrees) turns the elbow/knee around the limb axis.
public native func APM_SetIkChain(chain: CName, enabled: Bool, x: Float, y: Float, z: Float, swivel: Float) -> Bool
// (offsetX, offsetY, offsetZ, swivel)
public native func APM_GetIkChain(chain: CName) -> Vector4
public native func APM_IsIkChainEnabled(chain: CName) -> Bool

// Master switch: editing a bone or loading a pose turns it on, leaving Photo Mode turns it off.
public native func APM_SetEnabled(enabled: Bool) -> Void
public native func APM_IsEnabled() -> Bool

// Pose files: red4ext/plugins/AdvancedPoseMod/poses/<name>.json
public native func APM_SavePose(name: String) -> Bool
public native func APM_LoadPose(name: String) -> Bool
// Refreshes the list of saved poses and returns its size
public native func APM_GetPoseCount() -> Int32
public native func APM_GetPoseName(index: Int32) -> String

// Photo Mode menu items (see PhotoModeUi.reds); attribute keys 7700-7799
public native func APM_UiIsOwnAttribute(attribute: Uint32) -> Bool
public native func APM_UiSetValue(attribute: Uint32, value: Float) -> Void
public native func APM_UiGetValue(attribute: Uint32) -> Float
public native func APM_UiIsAttributeEnabled(attribute: Uint32) -> Bool
public native func APM_UiConsumeRefresh() -> Bool
public native func APM_UiRequestRefresh() -> Void
public native func APM_UiGetOptionCount(attribute: Uint32) -> Int32
public native func APM_UiGetOptionLabel(attribute: Uint32, index: Int32) -> String
public native func APM_UiGetOptionGroup(attribute: Uint32, index: Int32) -> String
public native func APM_UiGetSlotCount() -> Int32
public native func APM_UiIsSlotSaved(slot: Int32) -> Bool

// Pose library
public native func APM_DeletePose(name: String) -> Bool
// Saves under the first free name ("Pose 001", ...) and returns it; empty on failure
public native func APM_SaveNewPose() -> String
// Renames a saved pose; returns the new name (made unique), or "" on failure
public native func APM_RenamePose(oldName: String, newName: String) -> String
// Saved full pose currently replacing the Photo Mode pose ("" if none)
public native func APM_GetBasePoseName() -> String
// Item records for the prop picker (0 weapons, 1 consumables, 2 other items), as TweakDBID numbers
public native func APM_PropRecordCount(kind: Int32) -> Int32
public native func APM_PropRecordAt(kind: Int32, index: Int32) -> Uint64
// TweakDBID from TDBID.ToNumber()
public native func APM_RecordFromNumber(number: Uint64) -> TweakDBID
// Items hidden from the item list by the player (kept in hidden_items.txt)
public native func APM_IsItemHidden(record: Uint64) -> Bool
public native func APM_HideItem(record: Uint64)
// Item held in a hand (TweakDBID number, 0 = none); saved with poses
public native func APM_SetHandProp(rightHand: Bool, record: Uint64)
public native func APM_GetHandProp(rightHand: Bool) -> Uint64
// Undo / redo: record the state when an edit starts
public native func APM_HistoryCheckpoint()
public native func APM_Undo() -> Bool
public native func APM_Redo() -> Bool
public native func APM_CanUndo() -> Bool
public native func APM_CanRedo() -> Bool
// Ctrl+Z / Ctrl+Y since the last call: 1 undo, 2 redo, 0 none
public native func APM_PollHotkey() -> Int32
// Copies one side onto the other, mirrored; group "Left arm" etc. or "" for the whole body. Returns parts copied.
public native func APM_MirrorParts(fromLeft: Bool, group: String) -> Int32
// Mirror mode: edits of a left / right part are copied, mirrored, to the other side
public native func APM_SetMirror(mirror: Bool)
public native func APM_IsMirror() -> Bool
// Replaces the Photo Mode pose with a T pose (or an A pose), clearing the edits
public native func APM_SetNeutralPose(armsDown: Bool) -> Bool
// Turns a part (an eye) to look at a model-space point
public native func APM_GizmoAimPart(part: Int32, modelTarget: Vector4) -> Bool
// Mouse wheel kept from the game while captured; ConsumeWheel returns the sum since the last call (120 per notch)
public native func APM_SetWheelCapture(capture: Bool)
public native func APM_ConsumeWheel() -> Int32
// Keyboard text entry: while typing, keys go to the text instead of the game. Enter confirms, Esc cancels.
public native func APM_TextEntryBegin(text: String) -> Bool
public native func APM_TextEntryEnd()
// 0 inactive, 1 typing, 2 confirmed, 3 cancelled
public native func APM_TextEntryGetState() -> Int32
public native func APM_TextEntryGetText() -> String

// Writes a line to the plugin log (red4ext/logs/advancedposemod-*.log)
public native func APM_LogInfo(text: String) -> Bool

// Panel preferences (panel.ini)
public native func APM_PrefGet(key: String, fallback: Float) -> Float
public native func APM_PrefSet(key: String, value: Float) -> Bool

// Gizmos
// Model-space position (relative to V: X right, Y forward, Z up) of the main bone of a body part; w = 1 when valid
public native func APM_GizmoGetPartPosition(part: Int32) -> Vector4
// Model-space point where a part's gizmo is drawn (the joint, or halfway along palm bones); w = 1 when valid
public native func APM_GizmoGetPartMarker(part: Int32) -> Vector4
// Model-space point the main bone of a body part points at (its child); w = 1 when valid
public native func APM_GizmoGetPartTip(part: Int32) -> Vector4
// IK chain whose upper arm / forearm (thigh / shin) is this part, or -1
public native func APM_GizmoGetPartSwivelChain(part: Int32) -> Int32
// True when the custom pose is on and the IK chain pins its hand / foot
public native func APM_GizmoIsIkActive(chain: Int32) -> Bool
// Model-space joint of an IK chain (0 shoulder / hip, 1 elbow / knee, 2 hand / foot); w = 1 when valid
public native func APM_GizmoGetChainJoint(chain: Int32, joint: Int32) -> Vector4
// True for parts only edited from the close-up gizmos (outer finger segments)
public native func APM_GizmoIsPartDetail(part: Int32) -> Bool
// Model-space direction a part looks along (e.g. an eye's gaze); w = 1 when valid
public native func APM_GizmoGetPartForward(part: Int32) -> Vector4
// Model-space joint of the closest parent part that has a gizmo (thigh -> hips); w = 1 when valid
public native func APM_GizmoGetPartParent(part: Int32) -> Vector4
// Model-space movement of an IK pin -> change of its offset (differs when the limb is pinned to the body)
public native func APM_GizmoIkModelToOffset(chain: Int32, modelDelta: Vector4) -> Vector4
// Moves a part that allows it (hips, held props) by a model-space vector (meters)
public native func APM_GizmoMovePart(part: Int32, modelDelta: Vector4) -> Bool
public native func APM_GizmoCanMovePart(part: Int32) -> Bool
// V's Photo Mode puppet (null when unknown)
public native func APM_GizmoGetEntity() -> ref<Entity>
// Animated component of V's main skeleton; GetLocalToWorld() maps bone model space to the world (null when unknown)
public native func APM_GizmoGetComponent() -> ref<IPlacedComponent>
// IK chain (0 left hand, 1 right hand, 2 left foot, 3 right foot) moved by dragging this part, or -1
public native func APM_GizmoGetPartIkChain(part: Int32) -> Int32
// Rotates a body part around a model-space axis (degrees); the panel angles follow
public native func APM_GizmoRotatePart(part: Int32, modelAxis: Vector4, degrees: Float) -> Bool
// Moves an IK target by a model-space vector (meters) and pins it
public native func APM_GizmoMoveIk(chain: Int32, modelDelta: Vector4) -> Bool
