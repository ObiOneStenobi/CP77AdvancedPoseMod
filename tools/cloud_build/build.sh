#!/bin/bash
# Builds AdvancedPoseMod.dll from the project sources into $APM_BUILD/AdvancedPoseMod.dll.
# The version comes from CMakeLists.txt (project VERSION). Warnings and errors are printed per file.
set -e
source "$(dirname "$0")/env.sh"
cd "$APM_BUILD"
VERSION=$(grep -m1 -oP '^\s*VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$APM_PROJECT/CMakeLists.txt")
IFS=. read -r MAJOR MINOR PATCH <<< "$VERSION"
FLAGS="$CL_FLAGS /I$APM_PROJECT/src /utf-8 /DAPM_VERSION_STRING=\"$VERSION\" /DAPM_VERSION_MAJOR=$MAJOR /DAPM_VERSION_MINOR=$MINOR /DAPM_VERSION_PATCH=$PATCH"
rm -f obj/app_*.obj obj/app_*.log
for f in $(find "$APM_PROJECT/src" -name '*.cpp'); do
  n=$(basename "$f" .cpp)
  ( clang $FLAGS /c "$f" /Foobj/app_$n.obj 2>&1 | grep -E "error|warning" > obj/app_$n.log || true ) &
done
wait
cat obj/app_*.log
lld-link /dll /out:AdvancedPoseMod.dll /machine:x64 /libpath:winsdk/crt/lib/x86_64 /libpath:winsdk/sdk/lib/um/x86_64 \
  /libpath:winsdk/sdk/lib/ucrt/x86_64 obj/*.obj kernel32.lib user32.lib advapi32.lib shell32.lib
echo "Built $APM_BUILD/AdvancedPoseMod.dll ($VERSION)"
