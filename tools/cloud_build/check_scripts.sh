#!/bin/bash
# Compiles the project's redscript files against the game's script bundle, like the game does at start.
# Usage: check_scripts.sh <final.redscripts> [more bundles]
#   The bundle is <game>/r6/cache/final.redscripts (base game) or r6/cache/modded/final.redscripts.modded (with the
#   user's other mods); stage it from the user's computer first.
set -e
source "$(dirname "$0")/env.sh"
[ $# -ge 1 ] || { echo "Usage: $0 <final.redscripts> [more bundles]"; exit 1; }
work=$(mktemp -d)
mkdir -p "$work/src/AdvancedPoseMod"
cp "$APM_PROJECT"/scripts/AdvancedPoseMod/*.reds "$work/src/AdvancedPoseMod/"
# Codeware (required by the mod: localization, text overflow) compiles with them, as in the game.
cp -r "$APM_BUILD/ext/cp2077-codeware/scripts" "$work/src/Codeware"
for bundle in "$@"; do
  echo "== $bundle"
  "$APM_BUILD/ext/redscript05/target/release/redscript-cli" compile -s "$work/src" -b "$bundle" -o "$work/out.redscripts" 2>&1 \
    | grep -E "ERROR|WARN|success" || true
done
rm -rf "$work"
