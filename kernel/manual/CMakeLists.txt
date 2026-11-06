add_library(pixcake_manual
  src/manual_stroke_session.cpp
)
target_include_directories(pixcake_manual PUBLIC include)
target_link_libraries(pixcake_manual PUBLIC pixcake_support pixcake_warnings)
