add_library(pixcake_command
  src/command_gate.cpp
  src/photo_status.cpp
)
target_include_directories(pixcake_command PUBLIC include)
target_link_libraries(pixcake_command PUBLIC pixcake_capability pixcake_editdoc pixcake_exportq pixcake_project pixcake_persist pixcake_support Threads::Threads pixcake_warnings)
