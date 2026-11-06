add_library(pixcake_config
  src/config_store.cpp
)
target_include_directories(pixcake_config PUBLIC include)
target_link_libraries(pixcake_config PUBLIC pixcake_support pixcake_warnings)
