#!/bin/bash
# One-time setup of the Linux cloud build (Claude sessions): clang-cl cross compile of the DLL for Windows x64 and
# the redscript 0.5 compiler used to check the scripts. Same versions as the 1.1.0 build.
# Usage: bash tools/cloud_build/setup.sh        (then build.sh / check_scripts.sh)
set -e
source "$(dirname "$0")/env.sh"
mkdir -p "$APM_BUILD/ext" "$APM_BUILD/obj"
cd "$APM_BUILD"

# Compiler and linker (clang 18 + lld-link).
command -v clang >/dev/null && command -v lld-link >/dev/null || { apt-get update && apt-get install -y clang lld; }

# Windows SDK and MSVC CRT headers / libraries.
if [ ! -d winsdk ]; then
  curl -sSL https://github.com/Jake-Shadle/xwin/releases/download/0.6.5/xwin-0.6.5-x86_64-unknown-linux-musl.tar.gz | tar xz
  ./xwin-0.6.5-x86_64-unknown-linux-musl/xwin --accept-license splat --output winsdk
fi

# Sources: RED4ext.SDK, nlohmann/json, redscript (checker), Codeware (reference for localization).
[ -d ext/RED4ext.SDK ] || { git clone -q https://github.com/wopss/RED4ext.SDK ext/RED4ext.SDK && git -C ext/RED4ext.SDK checkout -q ad72777; }
# The SDK includes RED4ext/rtti/... but the folder is RED4ext/RTTI (fine on Windows, not on Linux).
[ -e ext/RED4ext.SDK/include/RED4ext/rtti ] || ln -s RTTI ext/RED4ext.SDK/include/RED4ext/rtti
[ -f ext/nlohmann/json.hpp ] || { mkdir -p ext/nlohmann && curl -sSL -o ext/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp; }
[ -d ext/redscript05 ] || { git clone -q https://github.com/jac3km4/redscript ext/redscript05 && git -C ext/redscript05 checkout -q v0.5.31; }
[ -d ext/cp2077-codeware ] || { git clone -q https://github.com/psiberx/cp2077-codeware ext/cp2077-codeware && git -C ext/cp2077-codeware checkout -q 613a1cb; }

# redscript compiler (needs Rust: https://rustup.rs if cargo is missing). Newer Rust versions turn some lints of
# redscript 0.5 into errors (#![deny(warnings)]): capped to warnings.
[ -x ext/redscript05/target/release/redscript-cli ] || (cd ext/redscript05 && RUSTFLAGS="--cap-lints warn" cargo build --release -q -p redscript-cli)

# RED4ext.SDK compiled once (static library sources); the app sources are compiled by build.sh.
n=0; failed=0
for f in $(find ext/RED4ext.SDK/src -name '*.cpp' | sort); do
  n=$((n + 1))
  out=obj/sdk_$(printf %03d $n).obj
  [ -f "$out" ] && continue
  clang $CL_FLAGS /c "$f" /Fo"$out" > /dev/null 2>&1 || { failed=$((failed + 1)); echo "SDK file not compiled (usually not needed): $f"; }
done
echo "Setup done in $APM_BUILD ($n SDK sources, $failed not compiled)."
