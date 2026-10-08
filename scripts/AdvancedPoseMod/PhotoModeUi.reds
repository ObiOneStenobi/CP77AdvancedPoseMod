// AdvancedPoseMod - items added to the "Pose" page of the Photo Mode menu.
//
// The game builds the menu natively and calls the script callbacks of gameuiPhotoModeMenuController
// (OnAddMenuItem, OnSetupScrollBar, ...). We append our own items with attribute keys 7700-7799 and
// forward every change of those items to the plugin (APM_UiSetValue). All the logic lives in the plugin;
// this file only builds and redraws the widgets. Only @wrapMethod/@addMethod are used, so other
// Photo Mode mods keep working.

module AdvancedPoseMod.Ui

// Pose page index (0 = camera, 1 = time & weather, 2 = pose).
public func APM_PosePage() -> Uint32 {
    return 2u;
}

// Item kinds
public func APM_KindSelector() -> Int32 {
    return 0;
}

public func APM_KindSlider() -> Int32 {
    return 1;
}

public struct APM_MenuItem {
    let attribute: Uint32;
    let label: String;
    let kind: Int32;
    let minValue: Float;
    let maxValue: Float;
    let step: Float;
}

public func APM_Selector(attribute: Uint32, label: String) -> APM_MenuItem {
    let item: APM_MenuItem;
    item.attribute = attribute;
    item.label = label;
    item.kind = APM_KindSelector();
    return item;
}

public func APM_Slider(attribute: Uint32, label: String, minValue: Float, maxValue: Float, step: Float) -> APM_MenuItem {
    let item: APM_MenuItem;
    item.attribute = attribute;
    item.label = label;
    item.kind = APM_KindSlider();
    item.minValue = minValue;
    item.maxValue = maxValue;
    item.step = step;
    return item;
}

// Must match src/Ui/PhotoModeUi.hpp
public func APM_MenuItems() -> array<APM_MenuItem> {
    let items: array<APM_MenuItem>;
    ArrayPush(items, APM_Selector(7700u, "Advanced pose"));
    ArrayPush(items, APM_Selector(7701u, "Body part"));
    ArrayPush(items, APM_Slider(7702u, "Bend forward / back", -180.0, 180.0, 1.0));
    ArrayPush(items, APM_Slider(7703u, "Bend sideways", -180.0, 180.0, 1.0));
    ArrayPush(items, APM_Slider(7704u, "Twist", -180.0, 180.0, 1.0));
    // Distances in centimeters: whole-number steps keep the sliders updating while dragging.
    ArrayPush(items, APM_Slider(7705u, "Move left / right (cm)", -100.0, 100.0, 1.0));
    ArrayPush(items, APM_Slider(7706u, "Move forward / back (cm)", -100.0, 100.0, 1.0));
    ArrayPush(items, APM_Slider(7707u, "Move up / down (cm)", -100.0, 100.0, 1.0));
    ArrayPush(items, APM_Selector(7721u, "Reset body part"));
    ArrayPush(items, APM_Selector(7710u, "IK limb"));
    ArrayPush(items, APM_Selector(7711u, "IK pin hand / foot"));
    ArrayPush(items, APM_Slider(7712u, "IK move right / left (cm)", -80.0, 80.0, 1.0));
    ArrayPush(items, APM_Slider(7713u, "IK move forward / back (cm)", -80.0, 80.0, 1.0));
    ArrayPush(items, APM_Slider(7714u, "IK move up / down (cm)", -80.0, 80.0, 1.0));
    ArrayPush(items, APM_Slider(7715u, "IK elbow / knee direction", -180.0, 180.0, 1.0));
    ArrayPush(items, APM_Selector(7720u, "Pose slot"));
    ArrayPush(items, APM_Selector(7723u, "Save pose to slot"));
    ArrayPush(items, APM_Selector(7724u, "Load pose from slot"));
    ArrayPush(items, APM_Selector(7722u, "Reset all"));
    return items;
}

// --- Menu construction -----------------------------------------------------------------------------------------------

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnAddingMenuItemsFinished() -> Bool {
    this.APM_AddItems();
    let result = wrappedMethod();
    this.APM_SetupItems();
    APM_UiRequestRefresh();
    return result;
}

@addMethod(gameuiPhotoModeMenuController)
private final func APM_AddItems() {
    let items = APM_MenuItems();
    let i = 0;
    while i < ArraySize(items) {
        let label = items[i].label;
        this.AddMenuItem(label, items[i].attribute, APM_PosePage(), false);
        i += 1;
    }
}

@addMethod(gameuiPhotoModeMenuController)
private final func APM_SetupItems() {
    let items = APM_MenuItems();
    let i = 0;
    while i < ArraySize(items) {
        let item = items[i];
        if item.kind == APM_KindSlider() {
            this.OnSetupScrollBar(item.attribute, APM_UiGetValue(item.attribute), item.minValue, item.maxValue, item.step, 0u);
        } else {
            this.APM_SetupSelector(item.attribute);
        }
        i += 1;
    }
}

@addMethod(gameuiPhotoModeMenuController)
private final func APM_SetupSelector(attribute: Uint32) {
    let values: array<PhotoModeOptionSelectorData>;
    let count = APM_UiGetOptionCount(attribute);
    let i = 0;
    while i < count {
        let option: PhotoModeOptionSelectorData;
        option.optionText = APM_UiGetOptionLabel(attribute, i);
        option.optionData = i;
        ArrayPush(values, option);
        i += 1;
    }
    this.OnSetupOptionSelector(attribute, values, Cast<Int32>(APM_UiGetValue(attribute)), false);
}

// --- Redraw when the plugin asks for it (other body part selected, pose loaded, ...) ---------------------------------

@wrapMethod(gameuiPhotoModeMenuController)
protected cb func OnUpdate(timeDelta: Float) -> Bool {
    let result = wrappedMethod(timeDelta);
    if APM_UiConsumeRefresh() {
        this.APM_RefreshItems();
    }
    this.APM_UpdatePoseEditor(timeDelta);
    return result;
}

@addMethod(gameuiPhotoModeMenuController)
private final func APM_RefreshItems() {
    let items = APM_MenuItems();
    let i = 0;
    while i < ArraySize(items) {
        let attribute = items[i].attribute;
        let listItem = this.GetMenuItem(attribute);
        if IsDefined(listItem) {
            // The slot list shows which slots are used: rebuild its options.
            if attribute == 7720u {
                this.APM_SetupSelector(attribute);
            }
            // Sliders snap to whole steps and report the snapped value back: remember what was forced so that echo
            // is not taken for a change (it moved gizmo edits by up to half a step on release).
            listItem.m_apmForcedValue = APM_UiGetValue(attribute);
            listItem.m_apmHasForcedValue = true;
            listItem.ForceValue(APM_UiGetValue(attribute), false);
            listItem.SetIsEnabled(APM_UiIsAttributeEnabled(attribute));
        }
        i += 1;
    }
}

// --- Forward value changes of our items to the plugin ----------------------------------------------------------------

@addMethod(PhotoModeMenuListItem)
private final func APM_ForwardValue() {
    let data = this.GetData() as PhotoModeMenuListItemData;
    if !IsDefined(data) || !APM_UiIsOwnAttribute(data.attributeKey) {
        return;
    }
    if IsDefined(this.m_OptionSelector) && this.m_OptionSelector.GetRootWidget().IsVisible() {
        let index = this.m_OptionSelector.GetCurrIndex();
        if index >= 0 && index < ArraySize(this.m_OptionSelectorValues) {
            APM_UiSetValue(data.attributeKey, Cast<Float>(this.m_OptionSelectorValues[index].optionData));
        }
    } else {
        let current = APM_UiGetValue(data.attributeKey);
        if this.m_apmHasForcedValue && AbsF(current - this.m_apmForcedValue) < 0.0001 && AbsF(this.m_SliderValue - this.m_apmForcedValue) < 0.51 {
            return; // the slider echoing a value set by the plugin, rounded to its step
        }
        this.m_apmHasForcedValue = false;
        APM_UiSetValue(data.attributeKey, this.m_SliderValue);
    }
}

@addField(PhotoModeMenuListItem)
public let m_apmForcedValue: Float;

@addField(PhotoModeMenuListItem)
public let m_apmHasForcedValue: Bool;

@wrapMethod(PhotoModeMenuListItem)
protected cb func OnScrollBarValueChanged(controller: wref<inkSliderController>, progress: Float, newValue: Float) -> Bool {
    let result = wrappedMethod(controller, progress, newValue);
    this.APM_ForwardValue();
    return result;
}

@wrapMethod(PhotoModeMenuListItem)
protected cb func OnOptionLeft(e: ref<inkPointerEvent>) -> Bool {
    let result = wrappedMethod(e);
    this.APM_ForwardValue();
    return result;
}

@wrapMethod(PhotoModeMenuListItem)
protected cb func OnOptionRight(e: ref<inkPointerEvent>) -> Bool {
    let result = wrappedMethod(e);
    this.APM_ForwardValue();
    return result;
}

@wrapMethod(PhotoModeMenuListItem)
public final func HandleReleasedInput(e: ref<inkPointerEvent>, opt gameCtrl: wref<inkGameController>) {
    wrappedMethod(e, gameCtrl);
    this.APM_ForwardValue();
}
