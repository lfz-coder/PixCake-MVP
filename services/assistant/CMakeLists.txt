# PixCake Assistant domain boundary.  This target deliberately has no Qt,
# HTTP, JSON, Python, OpenGL, or model-runtime dependency.  Transport adapters
# may translate a wire request into these provider-neutral records later.
add_library(pixcake_assistant
  src/assistant_channel.cpp
  src/assistant_events.cpp
  src/assistant_gateway.cpp
)
target_include_directories(pixcake_assistant PUBLIC include)
target_link_libraries(pixcake_assistant PUBLIC pixcake_preview_channel pixcake_support pixcake_warnings)
