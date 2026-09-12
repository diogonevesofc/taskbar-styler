add_library(styler_core STATIC
  version.cpp
  selector.cpp
)

target_include_directories(styler_core PUBLIC include)
target_link_libraries(styler_core PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(styler_core PRIVATE /W4 /permissive-)
endif()
