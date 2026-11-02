add_executable(test_support kernel/support/test_support.cpp)
target_link_libraries(test_support PRIVATE pixcake_support pixcake_warnings)
add_test(NAME test_support COMMAND test_support)
