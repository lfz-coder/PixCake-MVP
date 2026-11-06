# 生成进程（架构第 34、142、507 行）：不链接 Qt、不链接图形接口，也不链接图层文档（门禁规则 5、5c）。
add_library(pixcake_generated_service
  src/generation_service.cpp
)
target_include_directories(pixcake_generated_service PUBLIC include)
target_link_libraries(pixcake_generated_service PUBLIC
  pixcake_generate
  pixcake_ipc
  pixcake_ledger
  pixcake_persist
  pixcake_support
  pixcake_warnings
)

add_executable(pixcake_app_generated main.cpp)
target_link_libraries(pixcake_app_generated PRIVATE pixcake_generated_service pixcake_logging pixcake_warnings)
target_compile_features(pixcake_app_generated PRIVATE cxx_std_17)
