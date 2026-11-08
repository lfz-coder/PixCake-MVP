add_library(pixcake_capability
  src/capability_module.cpp
  src/capability_registry.cpp
  src/capability_sync.cpp
  src/fragment_codec.cpp
)
target_include_directories(pixcake_capability PUBLIC include)
target_link_libraries(pixcake_capability PUBLIC pixcake_support pixcake_mask pixcake_warnings)
