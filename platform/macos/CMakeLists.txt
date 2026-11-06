# macOS 平台适配（架构“目录”一节：platform/macos）。只有这里包含 CoreFoundation。
add_library(pixcake_platform_macos
  src/mac_source_access.cpp
)
target_include_directories(pixcake_platform_macos PUBLIC include)
target_link_libraries(pixcake_platform_macos PUBLIC pixcake_project pixcake_support pixcake_warnings
  PRIVATE "-framework CoreFoundation")
