// AdvancedPoseMod - interface texts and translations.
//
// Every text of the panel has a key ("APM-...") and an English default written here in the code. The text shown is
// the translation Codeware's localization system has for that key in the game's interface language; without one,
// the English default. A translation mod only adds a Codeware ModLocalizationProvider with a package for its language
// (template: tools/translation_template.reds); it never touches these files.
//
// Body parts, groups and IK limbs come from the DLL as English labels; their keys are built from those labels
// ("Left upper arm" -> "APM-Part-LeftUpperArm"). Texts with a value use "{0}" where the value goes.

module AdvancedPoseMod.Ui

import Codeware.Localization.*

public func APM_Text(key: String, english: String) -> String {
    let system = LocalizationSystem.GetInstance(GetGameInstance());
    if IsDefined(system) {
        let text = system.GetText(key);
        if StrLen(text) > 0 && NotEquals(text, key) {
            return text;
        }
    }
    return english;
}

// A text with a value: "{0}" in the text is replaced by the value.
public func APM_TextWith(key: String, english: String, value: String) -> String {
    return StrReplace(APM_Text(key, english), "{0}", value);
}

// "Left upper arm" -> "LeftUpperArm", "Eyes (both)" -> "EyesBoth", "Left palm - index" -> "LeftPalmIndex".
public func APM_KeyName(label: String) -> String {
    let result = "";
    let upper = true;
    let i = 0;
    let length = StrLen(label);
    while i < length {
        let ch = StrMid(label, i, 1);
        let isLetter = NotEquals(StrUpper(ch), StrLower(ch));
        let isDigit = StrFindFirst("0123456789", ch) >= 0;
        if isLetter || isDigit {
            result += upper ? StrUpper(ch) : ch;
            upper = false;
        } else {
            upper = true;
        }
        i += 1;
    }
    return result;
}

// Full name of a body part (the DLL's label), e.g. "Left upper arm".
public func APM_PartText(label: String) -> String {
    return APM_Text("APM-Part-" + APM_KeyName(label), label);
}

// Short name of a body part on its button (the tab already says which side), e.g. "Upper arm".
public func APM_PartShortText(label: String) -> String {
    let short = APM_ShortPartLabel(label);
    if Equals(short, label) {
        return APM_PartText(label);
    }
    return APM_Text("APM-PartShort-" + APM_KeyName(short), short);
}

// Body part group (tab), e.g. "Left arm".
public func APM_GroupText(group: String) -> String {
    return APM_Text("APM-Group-" + APM_KeyName(group), group);
}
