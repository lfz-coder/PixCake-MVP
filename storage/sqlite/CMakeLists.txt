find_library(PIXCAKE_SQLITE3 sqlite3 REQUIRED)

add_library(pixcake_storage
  src/sqlite_project_store.cpp
  src/sqlite_ledger_log.cpp
  src/sqlite_export_log.cpp
)
target_include_directories(pixcake_storage PUBLIC include)
target_link_libraries(pixcake_storage PUBLIC pixcake_project pixcake_ledger pixcake_exportq ${PIXCAKE_SQLITE3})
