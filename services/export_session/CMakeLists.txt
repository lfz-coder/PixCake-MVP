# The shell's export flow: enqueue through the command entry, hand tasks to
# exportd, read the export log back for the export list.  No Qt.
add_library(pixcake_export_session
  src/export_charge.cpp
  src/export_session.cpp
)
target_include_directories(pixcake_export_session PUBLIC include)
target_link_libraries(pixcake_export_session PUBLIC pixcake_project_session pixcake_export_client pixcake_command
  pixcake_exportq pixcake_editdoc pixcake_render pixcake_ledger pixcake_storage pixcake_persist pixcake_support pixcake_warnings)
# The picture fingerprint for export charging decodes a small thumbnail through ImageIO.
target_link_libraries(pixcake_export_session PRIVATE pixcake_platform_macos_image)
