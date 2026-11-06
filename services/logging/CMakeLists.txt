add_library(pixcake_logging
  src/file_log_sink.cpp
  src/process_log.cpp
)
target_include_directories(pixcake_logging PUBLIC include)
target_link_libraries(pixcake_logging PUBLIC pixcake_support pixcake_warnings)
