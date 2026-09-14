add_executable(styler_core_tests
  test_smoke.cpp
  test_selector.cpp
  test_style_rule.cpp
  test_theme_loader.cpp
  test_utf.cpp
  test_corpus.cpp
  test_constants.cpp
  test_type_name.cpp
  test_blur_rewrite.cpp
  test_blur.cpp
  test_corpus_resolution.cpp
  test_matcher.cpp
  test_style_expression.cpp
  test_config.cpp
  test_resource_variables.cpp
)

target_link_libraries(styler_core_tests PRIVATE
  styler_core
  doctest::doctest
  nlohmann_json::nlohmann_json
)

if(MSVC)
  # Same reasoning as src/core/CMakeLists.txt: /utf-8 for the non-ASCII
  # literals in this directory's tests, /W4 /permissive- so the test target
  # gets the same warning coverage as styler_core rather than a blind spot.
  target_compile_options(styler_core_tests PRIVATE /utf-8 /W4 /permissive-)
endif()

# Lets tests reach fixture files under tests/core/data regardless of the
# test binary's working directory when run. STYLER_THEMES_DIR follows the
# same pattern for the committed theme corpus under themes/.
target_compile_definitions(styler_core_tests PRIVATE
  STYLER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  STYLER_THEMES_DIR="${CMAKE_SOURCE_DIR}/themes"
)

add_test(NAME core COMMAND styler_core_tests)
