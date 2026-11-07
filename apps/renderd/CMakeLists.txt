add_library(pixcake_renderd_service
  src/image_io.cpp
  src/system_image_decoder.cpp
  src/face_detection.cpp
  src/segmentation.cpp
  src/segmentation_models.cpp
  src/semantic_label_adapter.cpp
  src/semantic_mask_provider.cpp
  src/render_service.cpp
  src/preview_server.cpp
)
target_include_directories(pixcake_renderd_service PUBLIC include)
target_link_libraries(pixcake_renderd_service PUBLIC
  pixcake_gpu_render
  pixcake_gpu_backend_native
  pixcake_ipc
  pixcake_mask
  pixcake_preview_channel
  pixcake_editdoc
  pixcake_render
  pixcake_support
  pixcake_warnings
)

# Inference adapters.  Only these files include LiteRT/XNNPACK/ONNX Runtime headers.
if(PIXCAKE_ENABLE_INFERENCE)
  add_library(pixcake_renderd_inference
    src/litert_model.cpp
    src/mediapipe_inference.cpp
    src/onnx_model.cpp
    src/portrait_inference.cpp
  )
  target_link_libraries(pixcake_renderd_inference
    PUBLIC pixcake_renderd_service
    PRIVATE tensorflow-lite XNNPACK pixcake_onnxruntime pixcake_warnings
  )
  target_compile_definitions(pixcake_renderd_inference
    PUBLIC PIXCAKE_HAS_INFERENCE=1
    PRIVATE
      PIXCAKE_MODEL_FILE_SELFIE_MULTICLASS="${PIXCAKE_MODEL_SELFIE_MULTICLASS_FILE}"
      PIXCAKE_MODEL_FILE_BLAZEFACE_FULL_RANGE="${PIXCAKE_MODEL_BLAZEFACE_FULL_RANGE_FILE}"
      PIXCAKE_MODEL_FILE_BISENET_RESNET18="${PIXCAKE_MODEL_BISENET_RESNET18_FILE}"
  )
endif()

if(APPLE)
  target_link_libraries(pixcake_renderd_service PRIVATE pixcake_platform_macos_image)
endif()

add_executable(pixcake_app_renderd main.cpp)
target_link_libraries(pixcake_app_renderd PRIVATE pixcake_renderd_service pixcake_logging pixcake_warnings)
if(PIXCAKE_ENABLE_INFERENCE)
  # Development fallback when the executable is not inside an app bundle.
  target_link_libraries(pixcake_app_renderd PRIVATE pixcake_renderd_inference)
  target_compile_definitions(pixcake_app_renderd PRIVATE PIXCAKE_DEFAULT_MODEL_DIR="${PIXCAKE_MODEL_DIR}")
endif()
target_compile_features(pixcake_app_renderd PRIVATE cxx_std_17)
