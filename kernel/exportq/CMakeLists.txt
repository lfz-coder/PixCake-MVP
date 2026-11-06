add_library(pixcake_exportq
  src/export_queue.cpp
  src/export_book.cpp
  src/export_settings.cpp
)
target_include_directories(pixcake_exportq PUBLIC include)
target_link_libraries(pixcake_exportq PUBLIC pixcake_persist pixcake_support pixcake_capability pixcake_warnings)
