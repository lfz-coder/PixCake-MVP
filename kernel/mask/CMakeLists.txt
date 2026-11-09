add_library(pixcake_mask
  src/combined_mask.cpp
  src/gradient_mask.cpp
  src/range_mask.cpp
  src/mask_sync.cpp
  src/semantic_parts.cpp
)
target_include_directories(pixcake_mask PUBLIC include)
target_link_libraries(pixcake_mask PUBLIC pixcake_warnings)
