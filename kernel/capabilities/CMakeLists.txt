# 每个专门的精修能力一个子目录（架构第 154 行）。子目录只链接 pixcake_capability 和 pixcake_support
#（架构第 196 行规则 4），互不包含（规则 2）。本层的 module_set 是唯一把它们放在一起的地方。
add_subdirectory(color_global)
add_subdirectory(detail)

add_library(pixcake_capability_modules module_set.cpp)
target_include_directories(pixcake_capability_modules PUBLIC include)
target_link_libraries(pixcake_capability_modules PUBLIC pixcake_capability
  PRIVATE pixcake_capability_color_global pixcake_capability_detail pixcake_warnings)
