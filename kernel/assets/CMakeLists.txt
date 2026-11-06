add_library(pixcake_assets
  src/asset_catalog.cpp
)
target_include_directories(pixcake_assets PUBLIC include)
target_link_libraries(pixcake_assets PUBLIC pixcake_support pixcake_warnings)
