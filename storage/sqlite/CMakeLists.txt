find_library(PIXCAKE_SQLITE3 sqlite3 REQUIRED)

add_library(pixcake_storage
  src/sqlite_project_store.cpp
)
target_include_directories(pixcake_storage PUBLIC include)
target_link_libraries(pixcake_storage PUBLIC pixcake_project ${PIXCAKE_SQLITE3})
