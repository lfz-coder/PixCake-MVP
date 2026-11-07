add_library(pixcake_exportd_service
  src/image_io.cpp
  src/export_service.cpp
  src/system_image_codec.cpp
  src/export_server.cpp
  src/export_masks.cpp
)
target_include_directories(pixcake_exportd_service PUBLIC include)
target_link_libraries(pixcake_exportd_service PUBLIC
  pixcake_editdoc
  pixcake_exportq
  pixcake_ipc
  pixcake_preview_channel
  # The preview's semantic segmentation (requirement 10.4: the same rules at
  # full resolution) and its preview-size decoder.
  pixcake_renderd_service
  pixcake_gpu_render
  pixcake_gpu_backend_native
  pixcake_render
  pixcake_support
  pixcake_warnings
)

if(PIXCAKE_ENABLE_INFERENCE)
  target_link_libraries(pixcake_exportd_service PUBLIC pixcake_renderd_inference)
endif()

# ImageIO decode and encode of the full-resolution original and product.
if(APPLE)
  target_link_libraries(pixcake_exportd_service PRIVATE pixcake_platform_macos_image)
endif()

add_executable(pixcake_app_exportd main.cpp)
target_link_libraries(pixcake_app_exportd PRIVATE pixcake_exportd_service pixcake_logging pixcake_storage
                      pixcake_warnings)
target_compile_features(pixcake_app_exportd PRIVATE cxx_std_17)
if(PIXCAKE_ENABLE_INFERENCE)
  target_compile_definitions(pixcake_app_exportd PRIVATE PIXCAKE_DEFAULT_MODEL_DIR="${PIXCAKE_MODEL_DIR}")
  # Inside the app bundle the runtime library is in Contents/Frameworks.
  target_link_options(pixcake_app_exportd PRIVATE "LINKER:-rpath,@executable_path/../Frameworks")
endif()
