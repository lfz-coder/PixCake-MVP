add_library(pixcake_ipc
  src/preview_protocol.cpp
)
target_include_directories(pixcake_ipc PUBLIC include)
target_link_libraries(pixcake_ipc PUBLIC pixcake_preview pixcake_support pixcake_warnings)
