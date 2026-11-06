add_library(pixcake_subject
  src/subject_store.cpp
)
target_include_directories(pixcake_subject PUBLIC include)
target_link_libraries(pixcake_subject PUBLIC pixcake_support pixcake_warnings)
