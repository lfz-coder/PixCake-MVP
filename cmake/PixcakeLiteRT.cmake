# LiteRT (TensorFlow Lite) runtime and the locked MediaPipe models.
#
# Included from cmake/PixcakeInference.cmake (PIXCAKE_ENABLE_INFERENCE=ON).
# Every source and model is pinned by commit/URL and SHA-256 in
# third_party/litert.lock.cmake; a hash
# mismatch fails configure.  Network access is needed on the first configure;
# set FETCHCONTENT_BASE_DIR to a shared directory (or
# FETCHCONTENT_SOURCE_DIR_PIXCAKE_LITERT / _PIXCAKE_TENSORFLOW to unpacked
# sources) to reuse downloads across build trees.
#
# Provides:
#   tensorflow-lite            LiteRT's own static library target
#   XNNPACK                    XNNPACK target (used for image resizing)

include_guard(GLOBAL)
include(FetchContent)
include(${CMAKE_SOURCE_DIR}/third_party/litert.lock.cmake)

enable_language(C)

FetchContent_Declare(pixcake_litert
  URL "${PIXCAKE_LITERT_SOURCE_URL}"
  URL_HASH "SHA256=${PIXCAKE_LITERT_SOURCE_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(pixcake_tensorflow
  URL "${PIXCAKE_TENSORFLOW_SOURCE_URL}"
  URL_HASH "SHA256=${PIXCAKE_TENSORFLOW_SOURCE_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Neither archive has a top-level CMakeLists.txt, so MakeAvailable only
# downloads, verifies and unpacks them.
FetchContent_MakeAvailable(pixcake_litert pixcake_tensorflow)

# Configure LiteRT's tflite/ project as a subdirectory with the locked options.
set(TENSORFLOW_SOURCE_DIR "${pixcake_tensorflow_SOURCE_DIR}" CACHE PATH "TensorFlow sources (locked)" FORCE)
set(TFLITE_ENABLE_XNNPACK ON CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_GPU OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_BENCHMARK_MODEL OFF CACHE BOOL "" FORCE)
add_subdirectory("${pixcake_litert_SOURCE_DIR}/tflite" "${CMAKE_BINARY_DIR}/_litert" EXCLUDE_FROM_ALL SYSTEM)
if(NOT TARGET tensorflow-lite OR NOT TARGET XNNPACK)
  message(FATAL_ERROR "LiteRT configure did not produce the tensorflow-lite and XNNPACK targets")
endif()

# Collect licence texts of LiteRT and everything it fetched.  A missing text
# is a configure error: the app must not ship code whose licence we cannot show.
set(_pixcake_notice_dir "${CMAKE_BINARY_DIR}/third_party_notices/litert")
file(MAKE_DIRECTORY "${_pixcake_notice_dir}")
set(_pixcake_notice_sources
  "LiteRT|${pixcake_litert_SOURCE_DIR}/LICENSE"
  "TensorFlow|${pixcake_tensorflow_SOURCE_DIR}/LICENSE"
  "abseil-cpp|${CMAKE_BINARY_DIR}/abseil-cpp/LICENSE"
  "flatbuffers|${CMAKE_BINARY_DIR}/flatbuffers/LICENSE"
  "XNNPACK|${CMAKE_BINARY_DIR}/xnnpack/LICENSE"
  "eigen-MPL2|${CMAKE_BINARY_DIR}/eigen/COPYING.MPL2"
  "ruy|${CMAKE_BINARY_DIR}/ruy/LICENSE"
  "gemmlowp|${CMAKE_BINARY_DIR}/gemmlowp/LICENSE"
  "cpuinfo|${CMAKE_BINARY_DIR}/cpuinfo/LICENSE"
  "pthreadpool|${CMAKE_BINARY_DIR}/pthreadpool-source/LICENSE"
  "FP16|${CMAKE_BINARY_DIR}/FP16-source/LICENSE"
  "FXdiv|${CMAKE_BINARY_DIR}/FXdiv-source/LICENSE"
  "farmhash|${CMAKE_BINARY_DIR}/farmhash/COPYING"
  "ml_dtypes|${CMAKE_BINARY_DIR}/ml_dtypes/LICENSE"
  "protobuf|${CMAKE_BINARY_DIR}/protobuf/LICENSE"
  "fft2d|${CMAKE_BINARY_DIR}/fft2d/readme2d.txt"
)
foreach(_entry IN LISTS _pixcake_notice_sources)
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _name)
  list(GET _parts 1 _path)
  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "LiteRT dependency licence text is missing: ${_name} (${_path})")
  endif()
  configure_file("${_path}" "${_pixcake_notice_dir}/${_name}.txt" COPYONLY)
endforeach()
file(SHA256 "${pixcake_litert_SOURCE_DIR}/LICENSE" _pixcake_litert_license_sha)
if(NOT _pixcake_litert_license_sha STREQUAL PIXCAKE_LITERT_SHA256_LICENSE)
  message(FATAL_ERROR "LiteRT LICENSE does not match the locked text (${_pixcake_litert_license_sha})")
endif()
