add_executable(taskbar_styler_tap_tests
  test_log.cpp
  test_loader.cpp
  test_tree_format.cpp
  test_release_policy.cpp
  test_dynamic_styles.cpp
  test_blur_effects.cpp
  test_thread_init.cpp
  test_handle_ledger.cpp
  ${CMAKE_SOURCE_DIR}/src/cli/loader.cpp
  ${CMAKE_SOURCE_DIR}/src/tap/blur_effects.cpp
  ${CMAKE_SOURCE_DIR}/src/tap/thread_init.cpp
)
target_include_directories(taskbar_styler_tap_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(taskbar_styler_tap_tests PRIVATE
  taskbar_styler_tap_lib
  styler_core
  WindowsApp.lib
  doctest::doctest
)
target_compile_definitions(taskbar_styler_tap_tests PRIVATE WINRT_LEAN_AND_MEAN)
# Scoped to test_log.cpp alone: this macro makes doctest.h emit its
# implementation (incl. main()). Defining it target-wide would emit that
# implementation again in every other TU that includes doctest.h - now that
# test_loader.cpp is a second such TU, that would fail to link with
# duplicate-symbol errors (main included).
set_source_files_properties(test_log.cpp PROPERTIES
  COMPILE_DEFINITIONS DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
)
if(MSVC)
  target_compile_options(taskbar_styler_tap_tests PRIVATE /utf-8 /W4 /permissive- /bigobj)
endif()

add_test(NAME tap COMMAND taskbar_styler_tap_tests)
