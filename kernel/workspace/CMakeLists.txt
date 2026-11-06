add_library(pixcake_workspace
  src/guide_copy.cpp
)
target_include_directories(pixcake_workspace PUBLIC include)
target_link_libraries(pixcake_workspace PUBLIC pixcake_support pixcake_warnings)
