cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "rule setup: SOURCE_DIR is required")
endif()

function(reject_if_contains path offense)
  if(IS_DIRECTORY "${path}")
    return()
  endif()
  file(READ "${path}" text)
  foreach(pattern IN LISTS ARGN)
    string(FIND "${text}" "${pattern}" found)
    if(NOT found EQUAL -1)
      message(FATAL_ERROR "${offense}: ${path} contains ${pattern}")
    endif()
  endforeach()
endfunction()

function(scan root offense)
  if(NOT EXISTS "${root}")
    return()
  endif()
  file(GLOB_RECURSE files "${root}/*")
  foreach(path ${files})
    reject_if_contains("${path}" "${offense}" ${ARGN})
  endforeach()
endfunction()

# 1. kernel 不包含 Qt 或 QOpenGL。
scan("${SOURCE_DIR}/kernel" "rule 1" "#include <Q" "#include <Qt" "QOpenGL" "#include \"Q")

# 2. 能力目录不包含另一个能力目录。
file(GLOB capability_dirs LIST_DIRECTORIES true "${SOURCE_DIR}/kernel/capabilities/*")
foreach(dir ${capability_dirs})
  if(NOT IS_DIRECTORY "${dir}")
    continue()
  endif()
  get_filename_component(own "${dir}" NAME)
  file(GLOB_RECURSE capability_files "${dir}/*")
  foreach(path ${capability_files})
    if(IS_DIRECTORY "${path}")
      continue()
    endif()
    file(READ "${path}" text)
    foreach(other ${capability_dirs})
      if(NOT IS_DIRECTORY "${other}")
        continue()
      endif()
      get_filename_component(other_name "${other}" NAME)
      if(other_name STREQUAL own)
        continue()
      endif()
      string(FIND "${text}" "capabilities/${other_name}" found)
      if(NOT found EQUAL -1)
        message(FATAL_ERROR "rule 2: ${path} includes capabilities/${other_name}")
      endif()
    endforeach()
  endforeach()
endforeach()

# 3. 渲染和公共外观不包含 Qt。
scan("${SOURCE_DIR}/gpu/render" "rule 3" "#include <Q" "#include <Qt" "QOpenGL")
scan("${SOURCE_DIR}/gpu/facade" "rule 3" "#include <Q" "#include <Qt" "QOpenGL")

# 4. native 后端不包含 Qt。
scan("${SOURCE_DIR}/gpu/backend/native" "rule 4" "#include <Q" "#include <Qt" "QOpenGL" "Qt6::" "Qt5::")

# 5. 生成进程不包含 Qt 或 OpenGL。
scan("${SOURCE_DIR}/apps/generated" "rule 5" "#include <Q" "#include <Qt" "QOpenGL" "OpenGL" "GL/gl.h")

# 6. 账本不包含图像解码或导出队列私有头。
scan("${SOURCE_DIR}/kernel/ledger" "rule 6" "jpeglib.h" "png.h" "opencv" "exportq/src")

# 7. 导出队列不包含能力私有头或账本实现头。
scan("${SOURCE_DIR}/kernel/exportq" "rule 7" "capabilities/" "ledger/src")

# 8. 界面不包含 GPU 后端头。
scan("${SOURCE_DIR}/ui" "rule 8" "gpu/backend")

# 9. 只有三个目标可以在自己的 CMake 里链接 Qt。
file(GLOB_RECURSE everything "${SOURCE_DIR}/*")
foreach(path ${everything})
  get_filename_component(name "${path}" NAME)
  if(NOT name STREQUAL "CMakeLists.txt")
    continue()
  endif()
  string(FIND "${path}" "/build/" in_build)
  if(NOT in_build EQUAL -1)
    continue()
  endif()
  file(READ "${path}" text)
  string(FIND "${text}" "Qt5::" qt5)
  string(FIND "${text}" "Qt6::" qt6)
  string(FIND "${text}" "Qt::" qt)
  if(qt5 EQUAL -1 AND qt6 EQUAL -1 AND qt EQUAL -1)
    continue()
  endif()
  set(allowed FALSE)
  foreach(prefix "/apps/shell/" "/apps/renderd/" "/gpu/backend/qt/")
    string(FIND "${path}" "${prefix}" allowed_at)
    if(NOT allowed_at EQUAL -1)
      set(allowed TRUE)
    endif()
  endforeach()
  if(NOT allowed)
    message(FATAL_ERROR "rule 9: ${path} links Qt outside the allowed targets")
  endif()
endforeach()

# 10. 公共 GPU 头的方法不使用 Qt 类型。
scan("${SOURCE_DIR}/gpu/facade" "rule 10" "QString" "QImage" "QOpenGL" "QWidget")

# 11. 同步模块不写死画笔或局部追色。原因只放在能力和蒙版的声明里。
scan("${SOURCE_DIR}/kernel/sync" "rule 11" "画笔" "局部追色")

# 12. 账本不链接 SQLite，也不抄工具点数表。
scan("${SOURCE_DIR}/kernel/ledger" "rule 12" "sqlite3.h" "面部重绘")

message(STATUS "arch dependencies ok")
