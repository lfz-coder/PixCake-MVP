add_library(pixcake_sync
  src/sync_batch.cpp
)
target_include_directories(pixcake_sync PUBLIC include)
target_link_libraries(pixcake_sync PUBLIC pixcake_capability pixcake_mask pixcake_editdoc pixcake_warnings)
