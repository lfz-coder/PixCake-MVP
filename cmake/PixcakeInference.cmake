# On-device inference: LiteRT (MediaPipe models), ONNX Runtime (BiSeNet) and
# the locked model files.  Included from the top-level CMakeLists.txt when
# PIXCAKE_ENABLE_INFERENCE=ON.

include_guard(GLOBAL)
include(${CMAKE_SOURCE_DIR}/cmake/PixcakeLiteRT.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/PixcakeOnnxRuntime.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/PixcakeModels.cmake)
