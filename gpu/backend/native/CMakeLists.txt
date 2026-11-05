find_package(OpenGL REQUIRED)

set(PIXCAKE_GLAD_SOURCE ${CMAKE_SOURCE_DIR}/third_party/glad/src/gl.c)
set_source_files_properties(${PIXCAKE_GLAD_SOURCE} PROPERTIES LANGUAGE CXX)
add_library(pixcake_glad STATIC
  ${PIXCAKE_GLAD_SOURCE}
)
target_include_directories(pixcake_glad PUBLIC
  ${CMAKE_SOURCE_DIR}/third_party/glad/include
)
target_link_libraries(pixcake_glad PUBLIC OpenGL::GL)

add_library(pixcake_gpu_backend_native
  src/native_backend.cpp
)
target_include_directories(pixcake_gpu_backend_native
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/gpu/facade/include
)
target_compile_definitions(pixcake_gpu_backend_native PUBLIC PIXCAKE_GL_BACKEND_NATIVE PRIVATE GL_SILENCE_DEPRECATION)
target_link_libraries(pixcake_gpu_backend_native PUBLIC pixcake_warnings pixcake_glad OpenGL::GL)
target_compile_features(pixcake_gpu_backend_native PUBLIC cxx_std_17)
