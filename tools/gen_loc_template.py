"""Builds tools/translation_template.reds: every interface text of AdvancedPoseMod with its key and English text.

Keys come from the scripts (APM_Text / APM_TextWith calls) and from the DLL's labels (body parts, groups, IK limbs:
keys built from the English label, as Localization.reds does). Run it after changing texts:

    python tools/gen_loc_template.py

The face controls are not shown yet: their texts are left out of the template until they are.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "scripts" / "AdvancedPoseMod"
OUT = ROOT / "tools" / "translation_template.reds"

# Not shown yet (face controls and saved expressions).
HIDDEN_PREFIXES = ("APM-Face", "APM-Tip-Face", "APM-Title-Face", "APM-Msg-Expression")
HIDDEN_KEYS = {"APM-Label-PhotoModeExpression", "APM-Label-NoExpressions", "APM-Button-Confirm", "APM-Msg-ClickAgainDelete"}

STRING = r'"((?:[^"\\]|\\.)*)"'
CALL = re.compile(r'(?:APM_Text(?:With)?\(|this\.L\(|TipL\([^,()]+,\s*)' + STRING + r',\s*' + STRING)
# Keys and English texts kept in two parallel arrays (hand preset tooltips).
ARRAYS = re.compile(r'let (\w+)Keys: array<String> = \[([^\]]*)\];\s*let \1Texts: array<String> = \[([^\]]*)\];')


def key_name(label):
    """Same as APM_KeyName in Localization.reds."""
    words = re.split(r"[^A-Za-z0-9]+", label)
    return "".join(w[:1].upper() + w[1:] for w in words if w)


def short_label(label):
    """Same as APM_ShortPartLabel in PoseEditor.reds."""
    if label.endswith(" eye"):
        return label
    for side in ("Left ", "Right "):
        if label.startswith(side):
            return label[len(side):]
    return label


def main():
    entries = {}  # key -> English, in order of first use
    sections = {}

    def add(key, english, section):
        if key.startswith(HIDDEN_PREFIXES) or key in HIDDEN_KEYS:
            return
        if key in entries and entries[key] != english:
            sys.exit(f"Key {key} has two English texts: {entries[key]!r} / {english!r}")
        if key not in entries:
            entries[key] = english
            sections.setdefault(section, []).append(key)

    for path in sorted(SCRIPTS.glob("*.reds")):
        text = path.read_text(encoding="utf-8")
        for match in CALL.finditer(text):
            add(match.group(1), match.group(2).replace('\\"', '"'), "Panel")
        for match in ARRAYS.finditer(text):
            for key, english in zip(re.findall(STRING, match.group(2)), re.findall(STRING, match.group(3))):
                add(key, english, "Panel")

    catalog = (ROOT / "src" / "Ui" / "BoneCatalog.cpp").read_text(encoding="utf-8")
    parts = re.findall(r'\{"([^"]+)", "([^"]+)", \{"', catalog)
    for group, _ in parts:
        add("APM-Group-" + key_name(group), group, "Body part groups (tabs)")
    for _, label in parts:
        add("APM-Part-" + key_name(label), label, "Body parts (full names)")
    ui = (ROOT / "src" / "Ui" / "PhotoModeUi.cpp").read_text(encoding="utf-8")
    limbs = re.search(r"IkLimbLabels\[\] = \{([^}]*)\}", ui).group(1)
    for label in re.findall(STRING, limbs):
        add("APM-Part-" + key_name(label), label, "Body parts (full names)")
    for _, label in parts:
        short = short_label(label)
        if short != label:
            add("APM-PartShort-" + key_name(short), short, "Body parts (short names on the buttons, the tab says the side)")

    lines = [
        "// AdvancedPoseMod - translation template",
        "//",
        "// How to make a translation mod (needs Codeware, like AdvancedPoseMod):",
        "// 1. Copy this file to r6\\scripts\\AdvancedPoseMod_<language>\\Translation.reds (e.g. AdvancedPoseMod_it).",
        "// 2. Rename the module and the two classes (replace \"xx\" with your language, e.g. \"it\"): every translation",
        "//    must have its own class names.",
        "// 3. Set the language code in GetPackage (the game's interface language: it-it, fr-fr, de-de, es-es, pl-pl,",
        "//    pt-br, ru-ru, zh-cn, zh-tw, jp-jp, kr-kr, ...).",
        "// 4. Translate the second text of every line. Keep \"{0}\": the value (a name) goes there. Lines left out show",
        "//    in English. Long texts on buttons shrink to fit; keep them short anyway.",
        "// The game shows the translation when its interface language matches; Photo Mode must be reopened after a",
        "// language change.",
        "",
        "module AdvancedPoseModTranslation.xx",
        "",
        "import Codeware.Localization.*",
        "",
        "public class APM_TranslationProvider_xx extends ModLocalizationProvider {",
        "    public func GetPackage(language: CName) -> ref<ModLocalizationPackage> {",
        "        return Equals(language, n\"xx-xx\") ? new APM_TranslationPackage_xx() : null;",
        "    }",
        "",
        "    public func GetFallback() -> CName {",
        "        return n\"\";",
        "    }",
        "}",
        "",
        "public class APM_TranslationPackage_xx extends ModLocalizationPackage {",
        "    protected func DefineTexts() {",
    ]
    for section, keys in sections.items():
        lines.append(f"        // {section}")
        for key in keys:
            english = entries[key].replace('"', '\\"')
            lines.append(f'        this.Text("{key}", "{english}");')
        lines.append("")
    if lines[-1] == "":
        lines.pop()
    lines += ["    }", "}", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"{OUT.relative_to(ROOT)}: {len(entries)} texts")


if __name__ == "__main__":
    main()
