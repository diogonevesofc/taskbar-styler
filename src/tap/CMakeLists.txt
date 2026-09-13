# The static half holds every source with no COM/XAML dependency (testable
# off-target); the MODULE half holds only sources that DO depend on COM/XAML
# and links the static lib. Every source lives in exactly one target, so a
# later task that adds a file "to both" cannot forget one half at link time.
add_library(taskbar_styler_tap_lib STATIC
  log.cpp
)
target_include_directories(taskbar_styler_tap_lib PUBLIC ..)

# tap_boundary.cpp is the only place XAML calls into our code: it defines the
# COM exports (DllGetClassObject / DllCanUnloadNow) and DllMain. It belongs
# here, never in the static lib.
add_library(taskbar_styler_tap MODULE
  tap_boundary.cpp
)
target_include_directories(taskbar_styler_tap PUBLIC ..)
target_link_libraries(taskbar_styler_tap PRIVATE taskbar_styler_tap_lib)
set_target_properties(taskbar_styler_tap PROPERTIES
  OUTPUT_NAME "TaskbarStyler.Tap"
  PREFIX ""
  SUFFIX ".dll"
)

# The .def controls exports explicitly: only DllGetClassObject and
# DllCanUnloadNow should be visible, nothing else.
target_link_options(taskbar_styler_tap PRIVATE
  "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/tap.def")

if(MSVC)
  target_compile_options(taskbar_styler_tap_lib PRIVATE /utf-8 /W4 /permissive-)
  target_compile_options(taskbar_styler_tap PRIVATE /utf-8 /W4 /permissive-)
endif()
