# Kernel 项目导入编排闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-kernel-project-import-closure.md`

## 已完成内容

- `ImportCoordinator` 拒绝空批次，按文件调用 `probe_source`，成功后才进入 `ProjectLibrary::add_photo`。
- 批次中的损坏、缺失、权限和项目状态冲突逐项返回；已经成功的照片记录不回滚，后续候选继续处理。
- 成功结果包含照片编号、内容格式、尺寸和 `FileIdentity`；访问凭证只传给项目库，不进入结果文本或日志。
- `pixcake_image_import` 依赖项目库、持久化和结果码，不依赖 Qt、OpenGL、SQLite、网络或像素解码器。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-kernel-project-import-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-project-import-20261105 -j2
ctest --test-dir /private/tmp/pixcake-kernel-project-import-20261105 --output-on-failure
```

结果：**38/38 CTest 通过**，包括 `test_import_coordinator`、`arch_dependencies` 和
`capability_ownership`；`git diff --check` 通过。

## 未覆盖范围

RAW 解码器、EXIF/XMP、缩略图、UI/拖放、云上传、安全范围书签、账本扣费、导出、
渲染和生成进程仍未实现。
