add_executable(taskbar_styler_cli
  main.cpp
  loader.cpp
)
target_include_directories(taskbar_styler_cli PRIVATE ..)
set_target_properties(taskbar_styler_cli PROPERTIES OUTPUT_NAME "taskbar-styler")
target_link_libraries(taskbar_styler_cli PRIVATE taskbar_styler_tap_lib ole32)

if(MSVC)
  target_compile_options(taskbar_styler_cli PRIVATE /utf-8 /W4 /permissive-)
endif()
