add_library(pixcake_render
  src/render_description.cpp
)
target_include_directories(pixcake_render PUBLIC include)
target_link_libraries(pixcake_render
  PUBLIC pixcake_layerdoc pixcake_editdoc pixcake_capability pixcake_support pixcake_warnings
  PRIVATE pixcake_capability_modules)
