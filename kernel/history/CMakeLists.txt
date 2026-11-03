add_library(pixcake_history
  src/history_log.cpp
)
target_include_directories(pixcake_history PUBLIC include)
target_link_libraries(pixcake_history PUBLIC pixcake_editdoc pixcake_warnings)
