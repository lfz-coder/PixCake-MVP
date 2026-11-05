add_library(pixcake_gpu_render src/render_executor.cpp)
target_include_directories(pixcake_gpu_render PUBLIC include)
target_include_directories(pixcake_gpu_render PUBLIC ${CMAKE_SOURCE_DIR}/kernel/render/include)
target_include_directories(pixcake_gpu_render PUBLIC
  ${CMAKE_SOURCE_DIR}/kernel/capability/include
  ${CMAKE_SOURCE_DIR}/kernel/editdoc/include
  ${CMAKE_SOURCE_DIR}/kernel/support/include
  ${CMAKE_SOURCE_DIR}/kernel/layerdoc/include)
target_include_directories(pixcake_gpu_render PUBLIC ${CMAKE_SOURCE_DIR}/kernel/persist/include)
target_link_libraries(pixcake_gpu_render PUBLIC pixcake_gpu_facade pixcake_warnings)
