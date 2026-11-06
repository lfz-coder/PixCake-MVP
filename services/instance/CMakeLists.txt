add_library(pixcake_instance
  src/single_instance.cpp
)
target_include_directories(pixcake_instance PUBLIC include)
target_link_libraries(pixcake_instance PUBLIC pixcake_support pixcake_warnings)
