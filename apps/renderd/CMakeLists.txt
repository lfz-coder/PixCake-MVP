add_library(pixcake_renderd_service
  src/image_io.cpp
  src/render_service.cpp
)
target_include_directories(pixcake_renderd_service PUBLIC include)
target_link_libraries(pixcake_renderd_service PUBLIC
  pixcake_gpu_render
  pixcake_ipc
  pixcake_render
  pixcake_support
  pixcake_warnings
)

add_executable(pixcake_app_renderd main.cpp)
target_link_libraries(pixcake_app_renderd PRIVATE pixcake_renderd_service pixcake_warnings)
target_compile_features(pixcake_app_renderd PRIVATE cxx_std_17)
