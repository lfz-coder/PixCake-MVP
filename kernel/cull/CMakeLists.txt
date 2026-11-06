add_library(pixcake_cull
  src/cull_session.cpp
)
target_include_directories(pixcake_cull PUBLIC include)
target_link_libraries(pixcake_cull PUBLIC pixcake_support pixcake_warnings)
