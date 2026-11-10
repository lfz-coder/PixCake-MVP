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

# 2. 能力目录不包含另一个能力目录（架构第 154、646 行：kernel/capabilities/ 下每个能力一个子目录）。
#    kernel/capabilities/include 与本层的 module_set 是能力集合，不是能力目录。
file(GLOB capability_dirs LIST_DIRECTORIES true "${SOURCE_DIR}/kernel/capabilities/*")
list(FILTER capability_dirs EXCLUDE REGEX "/kernel/capabilities/include$")
set(capability_dir_count 0)
foreach(dir ${capability_dirs})
  if(NOT IS_DIRECTORY "${dir}")
    continue()
  endif()
  math(EXPR capability_dir_count "${capability_dir_count} + 1")
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
    # 2b. 架构第 196 行规则 4：普通能力只用 kernel/capability、editdoc 只读视图、mask 编号、subject 只读视图、
    #     support。源文件只包含这些头，CMake 只链接这些库。
    string(REGEX MATCHALL "#include \"pixcake/[a-z_]+/" included "${text}")
    foreach(include ${included})
      string(REGEX REPLACE "#include \"pixcake/([a-z_]+)/" "\\1" module "${include}")
      if(NOT module MATCHES "^(capability|capabilities|editdoc|mask|subject|support)$")
        message(FATAL_ERROR "rule 2b: ${path} includes pixcake/${module}/")
      endif()
    endforeach()
    get_filename_component(file_name "${path}" NAME)
    if(file_name STREQUAL "CMakeLists.txt")
      string(REGEX MATCHALL "pixcake_[a-z_]+" linked "${text}")
      foreach(target ${linked})
        if(NOT target MATCHES "^pixcake_(capability|capability_${own}|editdoc|mask|subject|support|warnings)$")
          message(FATAL_ERROR "rule 2b: ${path} links ${target}")
        endif()
      endforeach()
    endif()
  endforeach()
endforeach()
if(capability_dir_count EQUAL 0)
  message(FATAL_ERROR "rule 2: kernel/capabilities/ has no capability directory")
endif()

# 3. 渲染和公共外观不包含 Qt。
scan("${SOURCE_DIR}/gpu/render" "rule 3" "#include <Q" "#include <Qt" "QOpenGL")
scan("${SOURCE_DIR}/gpu/facade" "rule 3" "#include <Q" "#include <Qt" "QOpenGL")

# 4. native 后端不包含 Qt。
scan("${SOURCE_DIR}/gpu/backend/native" "rule 4" "#include <Q" "#include <Qt" "QOpenGL" "Qt6::" "Qt5::")

# 5. 生成进程不包含 Qt 或 OpenGL。
scan("${SOURCE_DIR}/apps/generated" "rule 5" "#include <Q" "#include <Qt" "QOpenGL" "OpenGL" "GL/gl.h")
# 5c. 生成进程不链接 Qt、GPU、渲染或图层文档目标（架构第 507、552 行）；它也必须存在（契约 C-30）。
if(NOT EXISTS "${SOURCE_DIR}/apps/generated/CMakeLists.txt")
  message(FATAL_ERROR "rule 5c: apps/generated is missing")
endif()
reject_if_contains("${SOURCE_DIR}/apps/generated/CMakeLists.txt" "rule 5c" "Qt6::" "Qt5::" "pixcake_gpu"
                   "pixcake_render" "pixcake_layerdoc")

# 5b. Preview and export process services stay independent from Qt Widgets and
# raw OpenGL.  A backend can be added behind gpu/render later without leaking
# platform headers into these orchestration boundaries.
scan("${SOURCE_DIR}/apps/renderd" "rule 5b" "#include <Q" "#include <Qt" "QOpenGL" "OpenGL" "GL/gl.h")
scan("${SOURCE_DIR}/apps/exportd" "rule 5b" "#include <Q" "#include <Qt" "QOpenGL" "OpenGL" "GL/gl.h")

# 6. 账本不包含图像解码或导出队列私有头。
scan("${SOURCE_DIR}/kernel/ledger" "rule 6" "jpeglib.h" "png.h" "opencv" "exportq/src")

# 7. 导出队列不包含能力私有头或账本实现头。
scan("${SOURCE_DIR}/kernel/exportq" "rule 7" "capabilities/" "ledger/src" "pixcake/ledger/" "sqlite3.h")

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

# 13. 图层文档不保存像素，也不进账本或 SQLite。
scan("${SOURCE_DIR}/kernel/layerdoc" "rule 13" "sqlite3.h" "pixcake/ledger/" "jpeglib.h" "QOpenGL" "#include <Q")

# 14. 工作区展示不画窗口，也不进账本或 SQLite。
scan("${SOURCE_DIR}/kernel/workspace" "rule 14" "sqlite3.h" "pixcake/ledger/" "jpeglib.h" "QOpenGL" "#include <Q")

# 15. Kernel target build files must not link forbidden UI/GPU/runtime dependencies.
file(GLOB_RECURSE kernel_cmake "${SOURCE_DIR}/kernel/*/CMakeLists.txt")
foreach(path ${kernel_cmake})
  file(READ "${path}" text)
  # 图像库链接目标同样禁止（依赖规范第 13、32 行：图像 I/O 放在 renderd/exportd 或平台适配器）。
  foreach(token "Qt5::" "Qt6::" "OpenGL" "GL/" "opencv" "cuda" "torch" "onnxruntime"
                "PNG::" "JPEG::" "TIFF::" "WebP::" "libjpeg" "libpng" "libtiff" "libwebp" "turbojpeg"
                "vips" "libraw" "OpenImageIO" "libheif" "Exiv2"
                # 模型运行时只在 apps/renderd 的推理适配层（语义分割计划 Step 5d+）。
                "tensorflow-lite" "tflite" "litert" "XNNPACK" "xnnpack")
    string(FIND "${text}" "${token}" found)
    if(NOT found EQUAL -1)
      message(FATAL_ERROR "rule 15: ${path} contains forbidden kernel dependency ${token}")
    endif()
  endforeach()
endforeach()

# 16. 整个 kernel/ 不包含图像库头文件（依赖规范第 13 行；第四轮复审指出 rule 6/13/14 只覆盖三个目录）。
#     kernel/import 的文件头探测是记录在案的手写例外，只读签名和尺寸，不引入任何图像库。
scan("${SOURCE_DIR}/kernel" "rule 16"
     "<png.h>" "\"png.h\"" "<jpeglib.h>" "\"jpeglib.h\"" "<turbojpeg.h>" "<tiffio.h>" "<tiff.h>"
     "<webp/" "<vips/" "<libraw/" "<OpenImageIO/" "<libheif/" "<exiv2/"
     "\"tflite/" "<tflite/" "\"xnnpack.h\"" "<xnnpack.h>" "onnxruntime_cxx_api.h")

# 17. 用户发起的修改都从命令入口进入（架构 6.2、第七轮条件 2）。壳层和界面不直接调用文档提交、
#     预设应用、粘贴、同步或应用至精修，只调用 CommandGate。
foreach(root "${SOURCE_DIR}/apps/shell" "${SOURCE_DIR}/ui")
  scan("${root}" "rule 17" "commit_preview(" "commit_fragment_replacement(" "commit_baked_reset("
       "paste_committed(" "apply_checked(" "retry_failed(" "apply_to_original(" "PresetLibrary")
endforeach()

# 18. 发布构建不能直达业务页（安全清单 S-25）。壳层里 --page 选项和 ShellDisplay::dev_preview 只能
#     出现在 #ifdef PIXCAKE_DEV_PAGES 块里；块外出现即违规。
file(GLOB shell_sources "${SOURCE_DIR}/apps/shell/*.cpp" "${SOURCE_DIR}/apps/shell/*.hpp")
foreach(path ${shell_sources})
  file(STRINGS "${path}" shell_lines)
  set(depth 0)
  foreach(line IN LISTS shell_lines)
    if(depth GREATER 0)
      if(line MATCHES "^[ \t]*#[ \t]*if")
        math(EXPR depth "${depth} + 1")
      elseif(line MATCHES "^[ \t]*#[ \t]*endif")
        math(EXPR depth "${depth} - 1")
      endif()
    elseif(line MATCHES "^[ \t]*#[ \t]*ifdef[ \t]+PIXCAKE_DEV_PAGES")
      set(depth 1)
    else()
      string(FIND "${line}" "dev_preview(" found_preview)
      string(FIND "${line}" "QStringLiteral(\"page\")" found_page)
      if(NOT found_preview EQUAL -1 OR NOT found_page EQUAL -1)
        message(FATAL_ERROR "rule 18: ${path} uses a development page preview outside PIXCAKE_DEV_PAGES")
      endif()
    endif()
  endforeach()
endforeach()

# 19. 平台框架只在 platform/ 里（架构“目录”一节；安全清单 S-10 的书签由 platform/macos 实现）。
foreach(root "${SOURCE_DIR}/kernel" "${SOURCE_DIR}/ui" "${SOURCE_DIR}/apps" "${SOURCE_DIR}/services" "${SOURCE_DIR}/storage")
  scan("${root}" "rule 19" "<CoreFoundation/" "<Foundation/" "<AppKit/" "<Security/")
endforeach()

# 20. Cloud directory services stay outside the kernel and rendering/storage.
include("${SOURCE_DIR}/tests/arch/check_cloud_dependencies.cmake")
scan("${SOURCE_DIR}/kernel" "rule 20" "pixcake/cloud/" "pixcake/cloud_transport/" "pixcake/cloud_sync/" "pixcake_cloud")
scan("${SOURCE_DIR}/kernel" "rule 20" "pixcake/net/" "pixcake_net" "pixcake_http_curl" "curl/" "CURL::" "nlohmann")

message(STATUS "arch dependencies ok")
