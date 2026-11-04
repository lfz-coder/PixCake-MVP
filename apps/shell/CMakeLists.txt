find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(pixcake_app_shell
  main.cpp
)
set_target_properties(pixcake_app_shell PROPERTIES MACOSX_BUNDLE TRUE)
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
