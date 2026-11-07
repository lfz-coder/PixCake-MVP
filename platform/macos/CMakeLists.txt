# macOS 平台适配（架构“目录”一节：platform/macos）。只有这里包含 CoreFoundation。
add_library(pixcake_platform_macos
  src/mac_source_access.cpp
)
target_include_directories(pixcake_platform_macos PUBLIC include)
target_link_libraries(pixcake_platform_macos PUBLIC pixcake_project pixcake_support pixcake_warnings
  PRIVATE "-framework CoreFoundation")

# ImageIO decoding for renderd/exportd and encoding for exportd (evidence 2026-11-06-renderd-transport-and-decoder.md).
add_library(pixcake_platform_macos_image
  src/mac_image_decoder.cpp
  src/mac_image_encoder.cpp
)
target_include_directories(pixcake_platform_macos_image PUBLIC include)
target_link_libraries(pixcake_platform_macos_image PUBLIC pixcake_support pixcake_warnings
  PRIVATE "-framework CoreFoundation" "-framework CoreGraphics" "-framework ImageIO")
