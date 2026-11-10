// AdvancedPoseMod - native functions implemented in red4ext/plugins/AdvancedPoseMod/AdvancedPoseMod.dll
//
// Rotations are in degrees around the bone's local axes (applied Z * Y * X), on top of the current pose.
// Translations are in meters, in the parent bone's space, on top of the current pose.
// Bone names: see probe/rig_*.txt (V's main skeleton is the "root" component, 71 bones).

public native func APM_IsPhotoModeActive() -> Bool
// The player confirmed leaving Photo Mode: the plugin stops applying the pose before the game tears it down.
public native func APM_OnPhotoModeExit() -> Void
// Characters: 0 = V, 1-3 = the characters added in the scene (human ones only). The panel works on the selected one.
public native func APM_CharacterIsPresent(character: Int32) -> Bool
public native func APM_CharacterGetSelected() -> Int32
public native func APM_CharacterSelect(character: Int32) -> Void
public native func APM_CharacterGetEntity(character: Int32) -> ref<Entity>
public native func APM_SetPlayerEntity(entity: ref<Entity>)
public native func APM_CharacterHasEdits(character: Int32) -> Bool
// Main skeleton component of a character (its local-to-world is that character's model space).
public native func APM_CharacterGetComponent(character: Int32) -> ref<IPlacedComponent>
// The Photo Mode NPC page switched to another added character.
public native func APM_GetBoneCount() -> Int32
public native func APM_GetBoneName(index: Int32) -> CName

public native func APM_SetBoneRotation(bone: CName, x: Float, y: Float, z: Float) -> Bool
public native func APM_SetBoneTranslation(bone: CName, x: Float, y: Float, z: Float) -> Bool
// xyz = value, w = 1 if the bone currently has this override
public native func APM_GetBoneRotation(bone: CName) -> Vector4
public native func APM_GetBoneTranslation(bone: CName) -> Vector4
public native func APM_ResetBone(bone: CName) -> Void
// Clears the pose, IK, base pose and face weights (ResetBody keeps the face)
public native func APM_ResetAll() -> Void
public native func APM_ResetBody() -> Void

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

// Pose files: red4ext/plugins/AdvancedPoseMod/poses/<name>.json; names of files in subfolders include them ("Folder/Name")
public native func APM_SavePose(name: String) -> Bool
public native func APM_LoadPose(name: String) -> Bool
// Refreshes the list of saved poses and returns its size
public native func APM_GetPoseCount() -> Int32
public native func APM_GetPoseName(index: Int32) -> String

// Photo Mode menu items (see PhotoModeUi.reds); attribute keys 7700-7999
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
public native func APM_SaveNewPose(folder: String) -> String
// Renames a saved pose; returns the new name (made unique), or "" on failure
public native func APM_RenamePose(oldName: String, newName: String) -> String
// Saved full pose currently replacing the Photo Mode pose ("" if none)
public native func APM_GetBasePoseName() -> String
// Saved hand poses (palm and fingers; one file poses either hand): red4ext/plugins/AdvancedPoseMod/hands/<name>.json
public native func APM_GetHandPoseCount() -> Int32
public native func APM_GetHandPoseName(index: Int32) -> String
public native func APM_SaveHandPose(name: String, rightHand: Bool) -> Bool
public native func APM_SaveNewHandPose(folder: String, rightHand: Bool) -> String
public native func APM_LoadHandPose(name: String, rightHand: Bool) -> Bool
public native func APM_DeleteHandPose(name: String) -> Bool
public native func APM_RenameHandPose(oldName: String, newName: String) -> String
// Item records for the prop picker (0 weapons, 1 consumables, 2 other items), as TweakDBID numbers
public native func APM_PropRecordCount(kind: Int32) -> Int32
public native func APM_PropRecordAt(kind: Int32, index: Int32) -> Uint64
// TweakDBID from TDBID.ToNumber()
public native func APM_RecordFromNumber(number: Uint64) -> TweakDBID
// Item held in a hand (TweakDBID number, 0 = none); saved with poses
public native func APM_SetHandProp(rightHand: Bool, record: Uint64)
public native func APM_GetHandProp(rightHand: Bool) -> Uint64
// Undo / redo: record the state when an edit starts
public native func APM_HistoryCheckpoint()
public native func APM_Undo() -> Bool
public native func APM_Redo() -> Bool
public native func APM_CanUndo() -> Bool
public native func APM_CanRedo() -> Bool
// Ctrl+Z / Ctrl+Y since the last call: 1 undo, 2 redo; Ctrl+1 / 2 / 3: 3 / 4 / 5 gizmo Direct / Move / Rotate; Ctrl+4: 6 gizmo
// axes; 0 none
public native func APM_PollHotkey() -> Int32
// Alt key held: free (trackball) rotation while dragging a gizmo
public native func APM_IsAltDown() -> Bool
public native func APM_IsRightMouseDown() -> Bool
public native func APM_IsLeftMouseDown() -> Bool
// Copies one side onto the other, mirrored; group "Left arm" etc. or "" for the whole body. Returns parts copied.
public native func APM_MirrorParts(fromLeft: Bool, group: String) -> Int32
// Hand presets: 0 open, 1 relaxed, 2 fist, 3 point, 4 grip
public native func APM_ApplyHandPreset(rightHand: Bool, preset: Int32) -> Bool
// Mirror mode: edits of a left / right part are copied, mirrored, to the other side
public native func APM_SetMirror(mirror: Bool)
public native func APM_IsMirror() -> Bool
// Joint limits: rotations stay within natural ranges (on by default)
public native func APM_SetLimits(limits: Bool)
public native func APM_AreLimitsOn() -> Bool
// Replaces the Photo Mode pose with a T pose (or an A pose), clearing the edits
public native func APM_SetNeutralPose(armsDown: Bool) -> Bool
// Turns a part (an eye) to look at a model-space point
public native func APM_GizmoAimPart(part: Int32, modelTarget: Vector4) -> Bool
// Mouse wheel kept from the game while captured; ConsumeWheel returns the sum since the last call (120 per notch)
// The active camera in V's model space (position, view direction, up), every frame: full poses turn towards it when
// Photo Mode's "Look at camera" is on.
public native func APM_SetCameraModel(character: Int32, position: Vector4, forward: Vector4, up: Vector4)
// Face: the game's face poses (UI attributes 7800 + pose: 0-100, the Photo Mode expression's value until set; above 100
// for some expressions of other mods), saved expressions (the whole face). Zones: "brows", "eyelids", "nose", "cheeks", "lips", "jaw", "ears", "tongue"; side "l", "r" or "".
public native func APM_FaceAvailable() -> Bool
public native func APM_FacePoseCount() -> Int32
public native func APM_FacePoseName(pose: Int32) -> String
public native func APM_FacePoseZone(pose: Int32) -> String
public native func APM_FacePoseSide(pose: Int32) -> String
// The same pose on the other side, or -1
public native func APM_FacePoseMirror(pose: Int32) -> Int32
// L+R linked: a left / right pose's weight also goes to its mirror (on by default)
public native func APM_FaceSetLinked(linked: Bool)
public native func APM_FaceIsLinked() -> Bool
public native func APM_FaceSetUnlocked(unlocked: Bool)
// Set and different from the Photo Mode value
public native func APM_FaceIsEdited(pose: Int32) -> Bool
// Resets give the Photo Mode values back
public native func APM_FaceResetPose(pose: Int32)
public native func APM_FaceReset()
public native func APM_FaceGetLoadedName() -> String
public native func APM_FaceReloadList()
public native func APM_FaceListCount() -> Int32
public native func APM_FaceListName(index: Int32) -> String
public native func APM_FaceSave(name: String) -> Bool
public native func APM_FaceSaveNew(folder: String) -> String
public native func APM_FaceLoad(name: String) -> Bool
public native func APM_FaceDelete(name: String) -> Bool
// Renames a saved expression; returns the new name (made unique), or "" on failure
public native func APM_FaceRename(oldName: String, newName: String) -> String
// False when the game was updated and the mod could not hook into it.
public native func APM_IsReady() -> Bool
public native func APM_IsPlayerPosed() -> Bool
// Opens the folder of saved poses (0) or saved expressions (1).
public native func APM_OpenFolder(kind: Int32, folder: String) -> Bool
public native func APM_SetWheelCapture(capture: Bool)
public native func APM_ConsumeWheel() -> Int32
// Game window size in pixels: x = width, y = height, w = 1 when known.
public native func APM_GetViewportSize() -> Vector4
// Keyboard text entry: while typing, keys go to the text instead of the game. Enter confirms, Esc cancels.
public native func APM_TextEntryBegin(text: String) -> Bool
public native func APM_TextEntryEnd()
// 0 inactive, 1 typing, 2 confirmed, 3 cancelled
public native func APM_TextEntryGetState() -> Int32
public native func APM_TextEntryGetText() -> String

// Writes a line to the plugin log (red4ext/logs/advancedposemod-*.log)
public native func APM_LogInfo(text: String) -> Bool
// True if the body part has edits (rotations / moves, or its IK pin moved away from the pose)
public native func APM_UiIsPartEdited(part: Int32) -> Bool

// Panel preferences (panel.ini)
public native func APM_PrefGet(key: String, fallback: Float) -> Float
public native func APM_PrefSet(key: String, value: Float) -> Bool
// Values kept until the game closes (not written to panel.ini)
public native func APM_SessionGet(key: String, fallback: Float) -> Float
public native func APM_SessionSet(key: String, value: Float)

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
// True while Photo Mode's "Look at camera" turns the part (eyes, head and neck, back for the torso): not editable.
public native func APM_GizmoIsPartLocked(part: Int32) -> Bool
public native func APM_GizmoIsPartDetail(part: Int32) -> Bool
// Model-space direction of a part's rotation axis as posed now (0 bend forward / back, 1 sideways, 2 twist); w = 1 when valid
public native func APM_GizmoGetPartAxis(part: Int32, axis: Int32) -> Vector4
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
