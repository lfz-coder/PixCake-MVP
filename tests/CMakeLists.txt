add_executable(test_support kernel/support/test_support.cpp)
target_link_libraries(test_support PRIVATE pixcake_support pixcake_warnings)
add_test(NAME test_support COMMAND test_support)

add_executable(test_atomic_file kernel/persist/test_atomic_file.cpp)
target_include_directories(test_atomic_file PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_atomic_file PRIVATE pixcake_persist pixcake_warnings)
add_test(NAME test_atomic_file COMMAND test_atomic_file)
