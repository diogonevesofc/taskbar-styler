# The static half holds every source with no COM/XAML dependency (testable
# off-target); the MODULE half holds only sources that DO depend on COM/XAML
# and links the static lib. Every source lives in exactly one target, so a
# later task that adds a file "to both" cannot forget one half at link time.
add_library(taskbar_styler_tap_lib STATIC
  log.cpp
  tree_format.cpp
  release_policy.cpp
)
target_include_directories(taskbar_styler_tap_lib PUBLIC ..)

# tap_boundary.cpp is the only place XAML calls into our code: it defines the
# COM exports (DllGetClassObject / DllCanUnloadNow) and DllMain. It belongs
# here, never in the static lib.
add_library(taskbar_styler_tap MODULE
  tap_boundary.cpp
  thread_init.cpp
  visual_tree_watcher.cpp
  tree_export.cpp
  element_registry.cpp
  release_queue.cpp
  change_subscription.cpp
  style_engine.cpp
  property_setter.cpp
  theme_session.cpp
)
target_include_directories(taskbar_styler_tap PUBLIC ..)
target_link_libraries(taskbar_styler_tap PRIVATE taskbar_styler_tap_lib)

# theme_session.cpp calls into styler_core (PrepareTheme, LoadThemeFromFile,
# ParseConfigJson) and uses SHGetKnownFolderPath/CoTaskMemFree for
# %APPDATA%\TaskbarStyler\config.json (shell32.lib, ole32.lib).
target_link_libraries(taskbar_styler_tap PRIVATE styler_core shell32.lib ole32.lib)

# C++/WinRT comes from the Windows SDK's pre-generated headers (no NuGet, no
# cppwinrt.exe step). WindowsApp.lib is the umbrella import library that
# resolves the RoXxx activation entry points those headers call. /bigobj
# because a TU that includes the XAML projection routinely exceeds the
# default 65k-section limit.
# Advapi32.lib resolves RegGetValueW: change_subscription.cpp's
# DisableCompositionDiag gate.
target_link_libraries(taskbar_styler_tap PRIVATE WindowsApp.lib Advapi32.lib)
target_compile_definitions(taskbar_styler_tap PRIVATE WINRT_LEAN_AND_MEAN)
target_compile_options(taskbar_styler_tap PRIVATE /bigobj)

set_target_properties(taskbar_styler_tap PROPERTIES
  OUTPUT_NAME "TaskbarStyler.Tap"
  PREFIX ""
  SUFFIX ".dll"
  # TapDllPath() (src/cli/loader.cpp) looks for the DLL next to the CLI exe.
  # CMake's default per-target output dirs would put this MODULE in
  # build/src/tap and the CLI exe in build/src/cli, so pin this one target's
  # output next to the CLI instead of moving the CLI (whose path is baked
  # into docs and the task-3 smoke test as build/src/cli/taskbar-styler.exe).
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/cli"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/cli"
)

# The .def controls exports explicitly: only DllGetClassObject and
# DllCanUnloadNow should be visible, nothing else.
target_link_options(taskbar_styler_tap PRIVATE
  "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/tap.def")

if(MSVC)
  target_compile_options(taskbar_styler_tap_lib PRIVATE /utf-8 /W4 /permissive-)
  target_compile_options(taskbar_styler_tap PRIVATE /utf-8 /W4 /permissive-)
endif()
