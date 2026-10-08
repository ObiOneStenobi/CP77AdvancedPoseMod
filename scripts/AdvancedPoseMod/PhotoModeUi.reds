// AdvancedPoseMod - Photo Mode integration.
//
// The pose editor is a panel of its own (PoseEditor.reds); nothing is added to the game's Photo Mode menu.
// This file only drives the panel from the menu controller's update. Only @wrapMethod/@addMethod are used, so
// other Photo Mode mods keep working.

module AdvancedPoseMod.Ui

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnUpdate(timeDelta: Float) -> Bool {
    let result = wrappedMethod(timeDelta);
    APM_UiConsumeRefresh(); // the panel refreshes on its own
    this.APM_UpdatePoseEditor(timeDelta);
    return result;
}
