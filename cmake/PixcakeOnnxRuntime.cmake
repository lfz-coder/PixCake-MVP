# ONNX Runtime: official macOS arm64 release package (third_party/onnxruntime.lock.cmake).
#
# Provides the imported target pixcake_onnxruntime.  Only macOS arm64 is in
# scope; other platforms fail configure instead of silently running without it.

include_guard(GLOBAL)
include(FetchContent)
include(${CMAKE_SOURCE_DIR}/third_party/onnxruntime.lock.cmake)

if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
  message(FATAL_ERROR "ONNX Runtime is locked to the macOS arm64 release package only")
endif()

FetchContent_Declare(pixcake_onnxruntime_package
  URL "${PIXCAKE_ONNXRUNTIME_PACKAGE_URL}"
  URL_HASH "SHA256=${PIXCAKE_ONNXRUNTIME_PACKAGE_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pixcake_onnxruntime_package)
set(_pixcake_ort_root "${pixcake_onnxruntime_package_SOURCE_DIR}")

file(STRINGS "${_pixcake_ort_root}/VERSION_NUMBER" _pixcake_ort_version LIMIT_COUNT 1)
if(NOT _pixcake_ort_version STREQUAL PIXCAKE_ONNXRUNTIME_VERSION)
  message(FATAL_ERROR "ONNX Runtime package version ${_pixcake_ort_version} != locked ${PIXCAKE_ONNXRUNTIME_VERSION}")
endif()

# Licence and third-party notices of the binary we ship.
set(_pixcake_ort_notice_dir "${CMAKE_BINARY_DIR}/third_party_notices/onnxruntime")
file(MAKE_DIRECTORY "${_pixcake_ort_notice_dir}")
foreach(_pair "LICENSE|${PIXCAKE_ONNXRUNTIME_SHA256_LICENSE}" "ThirdPartyNotices.txt|${PIXCAKE_ONNXRUNTIME_SHA256_THIRD_PARTY_NOTICES}")
  string(REPLACE "|" ";" _parts "${_pair}")
  list(GET _parts 0 _file)
  list(GET _parts 1 _expected)
  file(SHA256 "${_pixcake_ort_root}/${_file}" _actual)
  if(NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "ONNX Runtime ${_file} does not match the locked text (${_actual})")
  endif()
  configure_file("${_pixcake_ort_root}/${_file}" "${_pixcake_ort_notice_dir}/${_file}" COPYONLY)
endforeach()

add_library(pixcake_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(pixcake_onnxruntime PROPERTIES
  IMPORTED_LOCATION "${_pixcake_ort_root}/${PIXCAKE_ONNXRUNTIME_LIBRARY}"
  IMPORTED_SONAME "@rpath/libonnxruntime.1.dylib"
  INTERFACE_INCLUDE_DIRECTORIES "${_pixcake_ort_root}/include"
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_pixcake_ort_root}/include"
)
set(PIXCAKE_ONNXRUNTIME_LIBRARY_DIR "${_pixcake_ort_root}/lib")
