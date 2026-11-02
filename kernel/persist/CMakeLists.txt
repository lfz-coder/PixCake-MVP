add_library(pixcake_persist
  src/atomic_file.cpp
  src/posix_file_system.cpp
)
target_include_directories(pixcake_persist PUBLIC include)
target_link_libraries(pixcake_persist PUBLIC pixcake_support pixcake_warnings)
