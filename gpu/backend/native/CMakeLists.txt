# Native Core OpenGL backend. It implements the same facade detail::*Impl
# contract as the Qt backend, so both can be built together and a caller picks
# one at runtime through its create_context() factory.
add_library(pixcake_gpu_backend_native src/native_backend.cpp)
target_include_directories(pixcake_gpu_backend_native PUBLIC include)
target_link_libraries(pixcake_gpu_backend_native PUBLIC pixcake_gpu_facade pixcake_warnings)

if(APPLE)
  find_package(OpenGL REQUIRED)
  set(PIXCAKE_GLAD_SOURCE ${CMAKE_SOURCE_DIR}/third_party/glad/src/gl.c)
  # The project enables only CXX; glad is C that also compiles as C++.
  set_source_files_properties(${PIXCAKE_GLAD_SOURCE} PROPERTIES LANGUAGE CXX)
  add_library(pixcake_glad STATIC ${PIXCAKE_GLAD_SOURCE})
  target_include_directories(pixcake_glad SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party/glad/include)
  target_link_libraries(pixcake_glad PUBLIC OpenGL::GL)
  target_compile_definitions(pixcake_gpu_backend_native PRIVATE GL_SILENCE_DEPRECATION)
  target_link_libraries(pixcake_gpu_backend_native PRIVATE pixcake_glad)
endif()
