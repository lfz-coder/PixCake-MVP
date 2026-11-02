add_library(pixcake_command
  src/command_gate.cpp
)
target_include_directories(pixcake_command PUBLIC include)
target_link_libraries(pixcake_command PUBLIC pixcake_editdoc pixcake_persist pixcake_support Threads::Threads pixcake_warnings)
