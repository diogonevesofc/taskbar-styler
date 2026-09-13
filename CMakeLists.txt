cmake_minimum_required(VERSION 3.25)
project(taskbar_styler LANGUAGES CXX)

# x64 only (Global Constraint): explorer.exe on any supported Windows 11
# build is x64, so an x86-configured TAP DLL can never load into it - it
# would build and the CLI would still report success, silently producing an
# artefact that cannot do the one thing this project exists to do.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "taskbar-styler is x64-only; configure from an x64 "
                       "developer prompt/toolset")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TASKBAR_STYLER_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

# GIT_TAG is pinned to the full commit SHA the v3.11.3 tag resolves to
# (verified via `git ls-remote --tags`), not the mutable tag name: a tag can
# be force-moved upstream, which would change what every future `cmake
# --fresh` configure fetches without this file changing at all.
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03)  # v3.11.3
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(src/core)
add_subdirectory(src/tap)
add_subdirectory(src/cli)

if(TASKBAR_STYLER_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG ae7a13539fb71f270b87eb2e874fbac80bc8dda2)  # v2.4.11
  # doctest v2.4.11 declares cmake_minimum_required(VERSION 3.0), which newer
  # CMake releases (4.0+) reject outright. CMAKE_POLICY_VERSION_MINIMUM lets
  # CMake apply policies as if the minimum were this version instead, without
  # patching the vendored dependency. Scoped tightly to this one call (set
  # right before, unset right after) so it cannot silently lower the policy
  # floor for any other dependency fetched later in this same directory scope
  # (e.g. a future FetchContent_MakeAvailable that legitimately requires a
  # stricter minimum would otherwise inherit this relaxed floor unnoticed).
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  FetchContent_MakeAvailable(doctest)
  unset(CMAKE_POLICY_VERSION_MINIMUM)

  enable_testing()
  add_subdirectory(tests/core)
  add_subdirectory(tests/tap)
endif()
