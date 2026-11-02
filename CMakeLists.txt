cmake_minimum_required(VERSION 3.20)
project(pixcake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(pixcake_warnings INTERFACE)
target_compile_options(pixcake_warnings INTERFACE -Wall -Wextra -Werror)

enable_testing()
add_subdirectory(kernel/support)
add_subdirectory(kernel/persist)
add_subdirectory(tests)
