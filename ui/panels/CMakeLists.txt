add_library(pixcake_ui_panels
  src/slider_spec.cpp
  src/histogram.cpp
  src/portrait_panel.cpp
)
target_include_directories(pixcake_ui_panels PUBLIC include)
target_link_libraries(pixcake_ui_panels PUBLIC pixcake_capability pixcake_ui_shell pixcake_warnings)
