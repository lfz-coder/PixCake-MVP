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
add_subdirectory(kernel/editdoc)
add_subdirectory(kernel/layerdoc)
add_subdirectory(kernel/workspace)
add_subdirectory(services/config)
add_subdirectory(services/account)
add_subdirectory(ui/shell)
add_subdirectory(apps/shell)
add_subdirectory(kernel/history)
add_subdirectory(kernel/preset)
add_subdirectory(kernel/capability)
add_subdirectory(kernel/assets)
add_subdirectory(kernel/cull)
add_subdirectory(kernel/render)
add_subdirectory(kernel/preview)
add_subdirectory(kernel/ipc)
add_subdirectory(kernel/generate)
add_subdirectory(kernel/import)
add_subdirectory(kernel/manual)
add_subdirectory(kernel/mask)
add_subdirectory(kernel/subject)
add_subdirectory(kernel/sync)
add_subdirectory(kernel/ledger)
add_subdirectory(kernel/exportq)
add_subdirectory(kernel/command)
add_subdirectory(kernel/project)
add_subdirectory(kernel/recipe)
add_subdirectory(storage/sqlite)
add_subdirectory(gpu/render)
add_subdirectory(gpu/backend/qt)
add_subdirectory(gpu/backend/native)
add_subdirectory(gpu/facade)
add_subdirectory(apps/renderd)
add_subdirectory(apps/exportd)
add_subdirectory(tests)

# Architecture rules are a build gate as well as a test.  A forbidden kernel
# dependency must fail the build before an artifact can be produced.
add_custom_target(architecture_gate ALL
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/arch/check_dependencies.cmake
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/arch/check_capability_ownership.cmake
  VERBATIM
)
