# Shell <-> renderd transport: Unix socket framing, fd passing, shared-memory
# frames (evidence 2026-11-06-renderd-transport-and-decoder.md).  No Qt.
add_library(pixcake_preview_channel
  src/channel.cpp
)
target_include_directories(pixcake_preview_channel PUBLIC include)
target_link_libraries(pixcake_preview_channel PUBLIC pixcake_support pixcake_warnings)

# The shell's client: kernel/ipc messages over the channel.
add_library(pixcake_preview_client
  src/preview_client.cpp
)
target_include_directories(pixcake_preview_client PUBLIC include)
target_link_libraries(pixcake_preview_client PUBLIC pixcake_preview_channel pixcake_ipc pixcake_warnings)

# One photo edited in the shell: renderd process, edit document through
# CommandGate, latest frame.
add_library(pixcake_preview_session
  src/preview_session.cpp
  src/thumbnail_service.cpp
)
target_include_directories(pixcake_preview_session PUBLIC include)
target_link_libraries(pixcake_preview_session PUBLIC pixcake_preview_client pixcake_command pixcake_history pixcake_editdoc
  pixcake_persist pixcake_capability pixcake_warnings)

# The shell's exportd process and connection (kernel/ipc process messages).
add_library(pixcake_export_client
  src/export_client.cpp
)
target_include_directories(pixcake_export_client PUBLIC include)
target_link_libraries(pixcake_export_client PUBLIC pixcake_preview_channel pixcake_ipc pixcake_warnings)
