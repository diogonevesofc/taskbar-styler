add_executable(taskbar_styler_cli
  main.cpp
  loader.cpp
)
target_include_directories(taskbar_styler_cli PRIVATE ..)
set_target_properties(taskbar_styler_cli PROPERTIES OUTPUT_NAME "taskbar-styler")
# Advapi32 for RegGetValueW (composition-diagnostics status), Shell32 for
# ShellExecuteExW (the `setup` command's elevated reg.exe).
target_link_libraries(taskbar_styler_cli PRIVATE taskbar_styler_tap_lib ole32
  Advapi32.lib Shell32.lib)

if(MSVC)
  target_compile_options(taskbar_styler_cli PRIVATE /utf-8 /W4 /permissive-)
endif()

# The CLI looks for themes next to its own exe (ThemesDir()). Mirror the
# repository's themes/ there after every build so a fresh build tree is
# runnable without a manual copy.
add_custom_command(TARGET taskbar_styler_cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/themes"
          "$<TARGET_FILE_DIR:taskbar_styler_cli>/themes")
