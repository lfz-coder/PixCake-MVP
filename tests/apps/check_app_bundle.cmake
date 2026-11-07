# The assembled app bundle carries its helper processes and, with inference,
# the runtime library, models and licence texts; the bundled helpers load the
# bundle's own copies (cmake/PixcakeBundle.cmake).
# Arguments: -DBUNDLE=<pixcake_app_shell.app> -DINFERENCE=ON|OFF -DLOG_DIR=<dir>
set(contents "${BUNDLE}/Contents")
foreach(helper pixcake_app_renderd pixcake_app_exportd)
  if(NOT EXISTS "${contents}/MacOS/${helper}")
    message(FATAL_ERROR "bundle lacks ${helper}")
  endif()
endforeach()
if(NOT INFERENCE)
  return()
endif()
foreach(path "Frameworks/libonnxruntime.1.dylib" "Resources/models/selfie_multiclass_256x256.tflite"
             "Resources/models/blaze_face_full_range.tflite" "Resources/models/bisenet_resnet18.onnx"
             "Resources/third_party_notices/litert/LiteRT.txt" "Resources/third_party_notices/onnxruntime/LICENSE")
  if(NOT EXISTS "${contents}/${path}")
    message(FATAL_ERROR "bundle lacks ${path}")
  endif()
endforeach()
file(GLOB model_licences "${contents}/Resources/third_party_notices/models/*")
if(NOT model_licences)
  message(FATAL_ERROR "bundle lacks the model licences")
endif()
foreach(helper exportd renderd)
  execute_process(COMMAND ${CMAKE_COMMAND} -E env DYLD_PRINT_LIBRARIES=1
                          "${contents}/MacOS/pixcake_app_${helper}" --health --log-dir "${LOG_DIR}"
                  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "bundled ${helper} --health failed (${status}): ${out}${err}")
  endif()
  if(NOT err MATCHES "Contents/Frameworks/libonnxruntime.1.dylib")
    message(FATAL_ERROR "bundled ${helper} did not load the bundle's runtime library: ${err}")
  endif()
  if(NOT "${out}${err}" MATCHES "semantic segmentation available; models [^\n]*Contents/Resources/models")
    message(FATAL_ERROR "bundled ${helper} did not use the bundle's models: ${out}${err}")
  endif()
endforeach()
