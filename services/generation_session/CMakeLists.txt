# Main-process result verification and layer placement.  Provider wire details
# never cross this boundary.
add_library(pixcake_generation_session
  src/generation_process.cpp
  src/generation_history.cpp
  src/generation_session.cpp
  src/generation_flow.cpp
  src/generation_result_applier.cpp
)
target_include_directories(pixcake_generation_session PUBLIC include)
target_link_libraries(pixcake_generation_session PUBLIC
  pixcake_generate
  pixcake_generated_service
  pixcake_command
  pixcake_ipc
  pixcake_layerdoc
  pixcake_persist
  pixcake_storage
  pixcake_support
  pixcake_warnings
)
