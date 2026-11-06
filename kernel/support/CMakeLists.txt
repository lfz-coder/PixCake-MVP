add_library(pixcake_support
  src/result_code.cpp
  src/checksum.cpp
  src/clock.cpp
  src/counting_log_sink.cpp
  src/user_notice.cpp
)
target_include_directories(pixcake_support PUBLIC include)
target_link_libraries(pixcake_support PUBLIC pixcake_warnings)
