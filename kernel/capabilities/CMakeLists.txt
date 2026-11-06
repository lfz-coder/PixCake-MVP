# 每个精修能力一个子目录（架构第 154 行）。子目录只有一个模块工厂，只链接 pixcake_capability
#（架构第 196 行规则 4），互不包含（规则 2）。module_set 是唯一把它们放在一起的地方。
function(add_capability_module name)
  add_library(pixcake_capability_${name} src/${name}_module.cpp)
  target_include_directories(pixcake_capability_${name} PUBLIC include)
  target_link_libraries(pixcake_capability_${name} PUBLIC pixcake_capability PRIVATE pixcake_warnings)
endfunction()

set(PIXCAKE_CAPABILITY_DIRS
  color_match
  color_style
  color_global
  color_local
  color_post
  detail
  portrait_blemish
  portrait_skin
  portrait_reshape
  portrait_feature
  portrait_body
  image_background
  image_idphoto
  image_sky
  clothes
  pet
  crop
  manual_edge
  manual_liquify
  manual_heal
)

add_library(pixcake_capability_modules module_set.cpp)
target_include_directories(pixcake_capability_modules PUBLIC include)
target_link_libraries(pixcake_capability_modules PUBLIC pixcake_capability PRIVATE pixcake_warnings)
foreach(dir ${PIXCAKE_CAPABILITY_DIRS})
  add_subdirectory(${dir})
  target_link_libraries(pixcake_capability_modules PRIVATE pixcake_capability_${dir})
endforeach()
