add_library(pixcake_ui_shell
  src/color_adjustment_panel.cpp
  src/shell_display.cpp
  src/page_copy.cpp
)
target_include_directories(pixcake_ui_shell PUBLIC include)
target_link_libraries(pixcake_ui_shell PUBLIC pixcake_support pixcake_warnings PRIVATE pixcake_workspace)
