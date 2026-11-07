# 开发登录下的主流程（需求 5.2/5.3）：从主页导入一个文件夹，项目以文件夹命名，精修出帧并可编辑，
# 图库列出照片；回主页单击项目进图库，双击照片进精修；精修顶栏导出成 JPG（需求第 19 节）；
# 拖到应用图标的三种情况（需求 3.2、B-71）。每次用全新的数据目录，不碰用户的项目库。
# 参数：-DEXE=<pixcake_app_shell> -DPHOTO=<图片> -DWORK=<临时目录>
file(REMOVE_RECURSE "${WORK}")
set(shoot "${WORK}/Shoot 01")
file(MAKE_DIRECTORY "${shoot}")
file(COPY_FILE "${PHOTO}" "${shoot}/portrait.png")
file(COPY_FILE "${PHOTO}" "${shoot}/portrait-2.png")
execute_process(COMMAND "${EXE}" --dev-sign-in --data-dir "${WORK}/data" --import "${shoot}" --smoke
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out MATCHES "icon drop ok" OR NOT out MATCHES "export ok: " OR NOT out MATCHES "import flow ok: project Shoot 01, 2 photo")
  message(FATAL_ERROR "import flow failed (${status}): ${out}${err}")
endif()
if(NOT EXISTS "${WORK}/data/projects.sqlite")
  message(FATAL_ERROR "project library was not written to the --data-dir")
endif()
file(GLOB documents "${WORK}/data/documents/photo-*.pxedit")
if(NOT documents)
  message(FATAL_ERROR "no edit document was committed")
endif()
file(REMOVE_RECURSE "${WORK}")
