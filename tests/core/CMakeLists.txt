add_executable(styler_core_tests
  test_smoke.cpp
)

target_link_libraries(styler_core_tests PRIVATE
  styler_core
  doctest::doctest
)

add_test(NAME core COMMAND styler_core_tests)
