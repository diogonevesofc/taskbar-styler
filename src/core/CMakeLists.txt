add_library(styler_core STATIC
  version.cpp
  selector.cpp
  style_rule.cpp
  utf.cpp
  theme_loader.cpp
  constants.cpp
  type_name.cpp
  blur_rewrite.cpp
  matcher.cpp
)

target_include_directories(styler_core PUBLIC include)
target_link_libraries(styler_core PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
  # /utf-8: sets both the source and execution character set to UTF-8. This
  # project's source files are UTF-8 without BOM and carry non-ASCII
  # literals (see tests/core/test_theme_loader.cpp); without this flag MSVC
  # falls back to the system codepage for both, which silently mojibakes
  # those literals (see tests/core/test_utf.cpp:40-44 for a measured case).
  target_compile_options(styler_core PRIVATE /utf-8 /W4 /permissive-)
endif()
