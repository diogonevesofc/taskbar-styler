add_executable(taskbar_styler_tap_tests
  test_log.cpp
)
target_link_libraries(taskbar_styler_tap_tests PRIVATE
  taskbar_styler_tap_lib
  doctest::doctest
)
target_compile_definitions(taskbar_styler_tap_tests PRIVATE
  DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
)
if(MSVC)
  target_compile_options(taskbar_styler_tap_tests PRIVATE /utf-8 /W4 /permissive-)
endif()

add_test(NAME tap COMMAND taskbar_styler_tap_tests)
