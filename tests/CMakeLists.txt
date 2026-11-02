add_executable(test_support kernel/support/test_support.cpp)
target_link_libraries(test_support PRIVATE pixcake_support pixcake_warnings)
add_test(NAME test_support COMMAND test_support)

add_executable(test_atomic_file kernel/persist/test_atomic_file.cpp)
target_include_directories(test_atomic_file PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_atomic_file PRIVATE pixcake_persist pixcake_warnings)
add_test(NAME test_atomic_file COMMAND test_atomic_file)

add_executable(test_edit_document kernel/editdoc/test_edit_document.cpp)
target_include_directories(test_edit_document PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_edit_document PRIVATE pixcake_editdoc pixcake_warnings)
add_test(NAME test_edit_document COMMAND test_edit_document)

add_executable(test_document_recovery recovery/test_document_recovery.cpp)
target_include_directories(test_document_recovery PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_document_recovery PRIVATE pixcake_editdoc pixcake_warnings)
add_test(NAME test_document_recovery COMMAND test_document_recovery)

add_executable(test_command_gate kernel/command/test_command_gate.cpp)
target_include_directories(test_command_gate PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_command_gate PRIVATE pixcake_command pixcake_warnings)
add_test(NAME test_command_gate COMMAND test_command_gate)

add_executable(test_project_paths kernel/project/test_project_paths.cpp)
target_include_directories(test_project_paths PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_project_paths PRIVATE pixcake_storage pixcake_warnings)
add_test(NAME test_project_paths COMMAND test_project_paths)

add_executable(test_source_presence kernel/project/test_source_presence.cpp)
target_include_directories(test_source_presence PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_source_presence PRIVATE pixcake_storage pixcake_editdoc pixcake_warnings)
add_test(NAME test_source_presence COMMAND test_source_presence)

add_test(
  NAME arch_dependencies
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/arch/check_dependencies.cmake
)
