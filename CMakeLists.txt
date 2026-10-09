cmake_minimum_required(VERSION 3.21)

project(
  AdvancedPoseMod
  VERSION 0.9.0
  DESCRIPTION "Bone-by-bone posing with IK for Cyberpunk 2077 Photo Mode"
  LANGUAGES CXX
)

if(NOT MSVC)
  message(FATAL_ERROR "AdvancedPoseMod must be built with MSVC (Visual Studio 2022, x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
include(FetchContent)

# RED4ext.SDK pinned to a known commit (compatible with RED4ext 1.30 / game 2.31).
FetchContent_Declare(
  red4ext_sdk
  GIT_REPOSITORY https://github.com/wopss/RED4ext.SDK.git
  GIT_TAG        ad7277714ad30d6885d7050c5ba24fa0102f6920
  GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(red4ext_sdk)

# nlohmann/json (header only) for pose files.
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# -----------------------------------------------------------------------------
# Plugin
# -----------------------------------------------------------------------------
file(GLOB_RECURSE APM_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/*.cpp"
  "${PROJECT_SOURCE_DIR}/src/*.hpp"
)

add_library(AdvancedPoseMod SHARED ${APM_SOURCES})

target_include_directories(AdvancedPoseMod PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(AdvancedPoseMod PRIVATE RED4ext::SDK nlohmann_json::nlohmann_json)

target_compile_definitions(AdvancedPoseMod PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  APM_VERSION_STRING="${PROJECT_VERSION}"
  APM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  APM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  APM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

target_compile_options(AdvancedPoseMod PRIVATE /W4 /permissive- /Zc:preprocessor /MP /utf-8)

set_target_properties(AdvancedPoseMod PROPERTIES OUTPUT_NAME "AdvancedPoseMod")

source_group(TREE "${PROJECT_SOURCE_DIR}/src" FILES ${APM_SOURCES})
