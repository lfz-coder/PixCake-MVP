if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(SYNC_SOURCE "${SOURCE_DIR}/kernel/capability/src/capability_sync.cpp")
file(READ "${SYNC_SOURCE}" sync_text)
foreach(label "AI 追色" "AI 色彩风格" "全图色彩工具" "局部调色" "后期调色"
              "祛除瑕疵" "皮肤调整" "面部重塑" "表情、牙齿、眼睛、妆容、头发"
              "手、脚、全身美型" "背景调整" "证件照换背景" "换天空" "衣物美化"
              "宠物美化" "裁剪旋转" "祛边" "液化" "污点修复、修补、仿制")
  string(FIND "${sync_text}" "${label}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "capability_sync.cpp owns registry label: ${label}")
  endif()
endforeach()
