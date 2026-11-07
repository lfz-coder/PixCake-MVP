find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt6Svg REQUIRED PATHS
  /opt/homebrew/opt/qtsvg/lib/cmake/Qt6Svg
  /usr/local/opt/qtsvg/lib/cmake/Qt6Svg)

add_executable(pixcake_app_shell
  main.cpp
  shell_window.cpp
  shell_controller.cpp
  page_kit.cpp
  shell_icons.cpp
  login_page.cpp
  home_page.cpp
  library_page.cpp
  refine_page.cpp
  export_dialog.cpp
  trash_dialog.cpp
  creative_page.cpp
  pixcake_widgets.hpp
)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/login-hero.png
               ${CMAKE_CURRENT_BINARY_DIR}/login-hero.png COPYONLY)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/login-hero.png
                            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(pixcake_app_shell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/login-hero.png)
file(GLOB_RECURSE PIXCAKE_SHELL_SVGS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.svg")
foreach(asset IN LISTS PIXCAKE_SHELL_SVGS)
  file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${asset}")
  get_filename_component(rel_dir "${rel}" DIRECTORY)
  set_source_files_properties("${asset}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/assets/${rel_dir}")
endforeach()
target_sources(pixcake_app_shell PRIVATE ${PIXCAKE_SHELL_SVGS})
set_target_properties(pixcake_app_shell PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.pixcake.desktop.shell"
  MACOSX_BUNDLE_BUNDLE_NAME "PixCake Desktop Shell"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
  # CMake's default template plus the document types for drops on the app icon.
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
)
target_link_libraries(pixcake_app_shell PRIVATE
  pixcake_instance
  pixcake_ui_shell
  pixcake_ui_panels
  pixcake_warnings
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Svg
)
target_compile_features(pixcake_app_shell PRIVATE cxx_std_17)
target_link_libraries(pixcake_app_shell PRIVATE pixcake_account pixcake_preview_session pixcake_project_session
  pixcake_export_session
  pixcake_platform_macos pixcake_logging)
# The shell starts renderd for previews; development builds use the build tree's copy.
add_dependencies(pixcake_app_shell pixcake_app_renderd)
target_compile_definitions(pixcake_app_shell PRIVATE PIXCAKE_RENDERD_DEV_PATH="$<TARGET_FILE:pixcake_app_renderd>")
# Exports go to exportd (architecture 10: the shell does not export at full resolution).
add_dependencies(pixcake_app_shell pixcake_app_exportd)
target_compile_definitions(pixcake_app_shell PRIVATE PIXCAKE_EXPORTD_DEV_PATH="$<TARGET_FILE:pixcake_app_exportd>")

# --page 直达业务页只在开发构建里有（安全清单 S-25）。发布构建由账号会话决定首屏。
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(PIXCAKE_DEV_PAGES_DEFAULT OFF)
else()
  set(PIXCAKE_DEV_PAGES_DEFAULT ON)
endif()
option(PIXCAKE_DEV_PAGES "Allow --page previews of business pages (development builds only)" ${PIXCAKE_DEV_PAGES_DEFAULT})
if(PIXCAKE_DEV_PAGES)
  target_compile_definitions(pixcake_app_shell PRIVATE PIXCAKE_DEV_PAGES=1)
endif()

add_test(NAME test_shell_app_help COMMAND pixcake_app_shell --help)
set_tests_properties(test_shell_app_help PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(PIXCAKE_DEV_PAGES)
  # Opens a real photo: renderd starts, the first frame and an exposure change reach the canvas.
  add_test(NAME test_shell_app_open_photo
           COMMAND pixcake_app_shell --open ${CMAKE_CURRENT_SOURCE_DIR}/login-hero.png --smoke)
  set_tests_properties(test_shell_app_open_photo PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  # The main flow with a development sign-in: import from home into a new project,
  # edit, library listing, back home, reopen.
  add_test(NAME test_shell_app_import_flow
           COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
                   ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:pixcake_app_shell>
                   -DPHOTO=${CMAKE_CURRENT_SOURCE_DIR}/login-hero.png
                   -DWORK=${CMAKE_CURRENT_BINARY_DIR}/import-flow
                   -P ${CMAKE_SOURCE_DIR}/tests/apps/check_shell_import_flow.cmake)
  set_tests_properties(test_shell_app_import_flow PROPERTIES TIMEOUT 180)
  foreach(shell_page IN ITEMS login home library refine creative)
    add_test(NAME test_shell_app_smoke_${shell_page}
             COMMAND pixcake_app_shell --page ${shell_page} --smoke)
    set_tests_properties(test_shell_app_smoke_${shell_page}
                         PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endforeach()
else()
  add_test(NAME test_shell_app_smoke_login COMMAND pixcake_app_shell --smoke)
  set_tests_properties(test_shell_app_smoke_login PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  # S-25：发布构建不认 --page，进不了业务页。
  add_test(NAME test_s25_release_shell_rejects_page COMMAND pixcake_app_shell --page home --smoke)
  set_tests_properties(test_s25_release_shell_rejects_page
                       PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" WILL_FAIL TRUE)
endif()

add_test(NAME test_shell_single_instance_T21
         COMMAND ${CMAKE_SOURCE_DIR}/tests/apps/check_shell_single_instance.sh $<TARGET_FILE:pixcake_app_shell>)
set_tests_properties(test_shell_single_instance_T21 PROPERTIES TIMEOUT 60)
