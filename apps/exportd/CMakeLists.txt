add_library(pixcake_exportd_service
  src/image_io.cpp
  src/export_service.cpp
  src/system_image_codec.cpp
)
target_include_directories(pixcake_exportd_service PUBLIC include)
target_link_libraries(pixcake_exportd_service PUBLIC
  pixcake_editdoc
  pixcake_exportq
  pixcake_gpu_render
  pixcake_gpu_backend_native
  pixcake_render
  pixcake_support
  pixcake_warnings
)

# ImageIO decode and encode of the full-resolution original and product.
if(APPLE)
  target_link_libraries(pixcake_exportd_service PRIVATE pixcake_platform_macos_image)
endif()

add_executable(pixcake_app_exportd main.cpp)
target_link_libraries(pixcake_app_exportd PRIVATE pixcake_exportd_service pixcake_logging pixcake_warnings)
target_compile_features(pixcake_app_exportd PRIVATE cxx_std_17)
