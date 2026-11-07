# Locked inference models and test fixtures (third_party/models.lock.cmake).
#
# Downloads each file once into <build>/models or <build>/testdata and checks
# its SHA-256 on every configure; a mismatch removes the file and fails.
#
# Provides:
#   PIXCAKE_MODEL_DIR          directory with the verified model files
#   PIXCAKE_TESTDATA_DIR       directory with verified test fixtures

include_guard(GLOBAL)
include(${CMAKE_SOURCE_DIR}/third_party/models.lock.cmake)

# Models and test fixtures: download once, verify the SHA-256 every configure.
set(PIXCAKE_MODEL_DIR "${CMAKE_BINARY_DIR}/models")
set(PIXCAKE_TESTDATA_DIR "${CMAKE_BINARY_DIR}/testdata")
function(pixcake_fetch_verified url sha256 destination)
  if(EXISTS "${destination}")
    file(SHA256 "${destination}" _actual)
    if(_actual STREQUAL sha256)
      return()
    endif()
    file(REMOVE "${destination}")
  endif()
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD "${url}" "${destination}" EXPECTED_HASH "SHA256=${sha256}" TLS_VERIFY ON STATUS _status)
  list(GET _status 0 _code)
  if(NOT _code EQUAL 0)
    file(REMOVE "${destination}")
    message(FATAL_ERROR "Download failed or hash mismatch for ${url}: ${_status}")
  endif()
endfunction()
foreach(_model IN LISTS PIXCAKE_MODEL_LIST)
  pixcake_fetch_verified("${PIXCAKE_MODEL_${_model}_URL}" "${PIXCAKE_MODEL_${_model}_SHA256}"
                         "${PIXCAKE_MODEL_DIR}/${PIXCAKE_MODEL_${_model}_FILE}")
endforeach()
pixcake_fetch_verified("${PIXCAKE_TESTDATA_PORTRAIT_URL}" "${PIXCAKE_TESTDATA_PORTRAIT_SHA256}"
                       "${PIXCAKE_TESTDATA_DIR}/${PIXCAKE_TESTDATA_PORTRAIT_FILE}")
