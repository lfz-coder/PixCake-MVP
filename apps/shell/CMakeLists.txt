find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(pixcake_app_shell
  main.cpp
  pixcake_widgets.hpp
)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/login-hero.png
               ${CMAKE_CURRENT_BINARY_DIR}/login-hero.png COPYONLY)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/login-hero.png
                            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(pixcake_app_shell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/login-hero.png)
set_target_properties(pixcake_app_shell PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.pixcake.desktop.shell"
  MACOSX_BUNDLE_BUNDLE_NAME "PixCake Desktop Shell"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
)
target_link_libraries(pixcake_app_shell PRIVATE
  pixcake_ui_shell
  pixcake_warnings
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)
target_compile_features(pixcake_app_shell PRIVATE cxx_std_17)

add_test(NAME test_shell_app_help COMMAND pixcake_app_shell --help)
set_tests_properties(test_shell_app_help PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

foreach(shell_page IN ITEMS login home library refine creative)
  add_test(NAME test_shell_app_smoke_${shell_page}
           COMMAND pixcake_app_shell --page ${shell_page} --smoke)
  set_tests_properties(test_shell_app_smoke_${shell_page}
                       PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
