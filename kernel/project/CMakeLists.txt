add_library(pixcake_project
  src/project_library.cpp
  src/file_source_deleter.cpp
)
target_include_directories(pixcake_project PUBLIC include)
target_link_libraries(pixcake_project PUBLIC pixcake_persist pixcake_support pixcake_warnings)
