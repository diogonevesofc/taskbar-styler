cmake_minimum_required(VERSION 3.25)
project(taskbar_styler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TASKBAR_STYLER_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(src/core)

if(TASKBAR_STYLER_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11)
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
endif()
