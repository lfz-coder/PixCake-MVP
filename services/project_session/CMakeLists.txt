# The shell's project flow (import, list, open originals) over the project
# library and command entry.  No Qt.
add_library(pixcake_project_session
  src/project_session.cpp
)
target_include_directories(pixcake_project_session PUBLIC include)
target_link_libraries(pixcake_project_session PUBLIC pixcake_command pixcake_editdoc pixcake_image_import pixcake_project
  pixcake_storage pixcake_persist pixcake_support pixcake_warnings)
