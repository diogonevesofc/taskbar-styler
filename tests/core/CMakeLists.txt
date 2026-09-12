add_executable(styler_core_tests
  test_smoke.cpp
  test_selector.cpp
  test_style_rule.cpp
  test_theme_loader.cpp
  test_utf.cpp
)

target_link_libraries(styler_core_tests PRIVATE
  styler_core
  doctest::doctest
)

# Lets tests reach fixture files under tests/core/data regardless of the
# test binary's working directory when run (matches the approach Task 7
# will use for the themes directory).
target_compile_definitions(styler_core_tests PRIVATE
  STYLER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

add_test(NAME core COMMAND styler_core_tests)
