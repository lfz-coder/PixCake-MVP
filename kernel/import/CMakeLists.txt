add_library(pixcake_image_import
  src/import_coordinator.cpp
  src/source_probe.cpp
)
target_include_directories(pixcake_image_import PUBLIC include)
target_link_libraries(pixcake_image_import PUBLIC pixcake_project pixcake_persist pixcake_support pixcake_warnings)
