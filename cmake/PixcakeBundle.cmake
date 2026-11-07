# The app bundle carries its helper processes (architecture 10: the shell
# starts renderd for previews and exportd for exports) so it runs outside the
# build tree:
#   Contents/MacOS/pixcake_app_renderd, pixcake_app_exportd
# and, with inference, what they load:
#   Contents/Frameworks/libonnxruntime.1.dylib   (rpath @executable_path/../Frameworks)
#   Contents/Resources/models/                    (model_directory_near_executable)
#   Contents/Resources/third_party_notices/       (LiteRT, ONNX Runtime, model licences)
# Qt itself is not deployed here (macdeployqt is a release step).
if(NOT APPLE OR NOT TARGET pixcake_app_shell)
  return()
endif()

set(_pixcake_contents "$<TARGET_BUNDLE_CONTENT_DIR:pixcake_app_shell>")
set(_pixcake_bundle_commands
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_pixcake_contents}/MacOS"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:pixcake_app_renderd>" "${_pixcake_contents}/MacOS/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:pixcake_app_exportd>" "${_pixcake_contents}/MacOS/"
)
if(PIXCAKE_ENABLE_INFERENCE)
  list(APPEND _pixcake_bundle_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_pixcake_contents}/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:pixcake_onnxruntime>"
            "${_pixcake_contents}/Frameworks/libonnxruntime.1.dylib"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PIXCAKE_MODEL_DIR}" "${_pixcake_contents}/Resources/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/third_party_notices"
            "${_pixcake_contents}/Resources/third_party_notices"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/models/LICENSES"
            "${_pixcake_contents}/Resources/third_party_notices/models"
  )
endif()
add_custom_target(pixcake_app_bundle ALL ${_pixcake_bundle_commands}
  COMMENT "Assembling pixcake_app_shell.app (helper processes, runtime library, models, notices)"
  VERBATIM)
add_dependencies(pixcake_app_bundle pixcake_app_shell pixcake_app_renderd pixcake_app_exportd)
