# The shell's export flow: enqueue through the command entry, hand tasks to
# exportd, read the export log back for the export list.  No Qt.
add_library(pixcake_export_session
  src/export_session.cpp
)
target_include_directories(pixcake_export_session PUBLIC include)
target_link_libraries(pixcake_export_session PUBLIC pixcake_project_session pixcake_export_client pixcake_command
  pixcake_exportq pixcake_editdoc pixcake_storage pixcake_persist pixcake_support pixcake_warnings)
