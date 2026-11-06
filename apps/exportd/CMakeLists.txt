add_library(pixcake_exportd_service
  src/export_service.cpp
)
target_include_directories(pixcake_exportd_service PUBLIC include)
target_link_libraries(pixcake_exportd_service PUBLIC
  pixcake_editdoc
  pixcake_exportq
  pixcake_gpu_render
  pixcake_render
  pixcake_support
  pixcake_warnings
)

add_executable(pixcake_app_exportd main.cpp)
target_link_libraries(pixcake_app_exportd PRIVATE pixcake_exportd_service pixcake_warnings)
target_compile_features(pixcake_app_exportd PRIVATE cxx_std_17)
