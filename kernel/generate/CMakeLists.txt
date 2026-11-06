add_library(pixcake_generate
  src/generation_task.cpp
)
target_include_directories(pixcake_generate PUBLIC include)
target_link_libraries(pixcake_generate PUBLIC pixcake_support pixcake_warnings)
