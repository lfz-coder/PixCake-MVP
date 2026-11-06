add_library(pixcake_ipc
  src/message_codec.cpp
  src/preview_protocol.cpp
  src/process_messages.cpp
)
target_include_directories(pixcake_ipc PUBLIC include)
target_link_libraries(pixcake_ipc PUBLIC pixcake_generate pixcake_preview pixcake_support pixcake_warnings
  PRIVATE pixcake_capability)
