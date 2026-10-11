# Shared settings for the cloud build scripts (sourced, not run).
# APM_BUILD: work folder for the toolchain, sources and objects (outside the project, about 1.5 GB).
APM_BUILD=${APM_BUILD:-$HOME/apm-build}
# The project (this file is in <project>/tools/cloud_build).
APM_PROJECT=${APM_PROJECT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
CL_FLAGS="--driver-mode=cl --target=x86_64-pc-windows-msvc -fms-compatibility-version=19.40 /std:c++20 /EHsc /MT /O1 /W4 \
 -Wno-unused-parameter -Wno-microsoft-cast -Wno-unknown-pragmas -Wno-nonportable-include-path -Wno-pragma-pack \
 /D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DRED4EXT_STATIC_LIB \
 -imsvc $APM_BUILD/winsdk/crt/include -imsvc $APM_BUILD/winsdk/sdk/include/ucrt -imsvc $APM_BUILD/winsdk/sdk/include/um \
 -imsvc $APM_BUILD/winsdk/sdk/include/shared -imsvc $APM_BUILD/ext/RED4ext.SDK/include \
 -imsvc $APM_BUILD/ext/RED4ext.SDK/vendor/D3D12MemAlloc -imsvc $APM_BUILD/ext"
