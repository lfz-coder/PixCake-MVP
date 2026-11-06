# Kernel 导入准入边界闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-kernel-import-admission-closure.md`

## 已完成内容

- `pixcake_image_import` 只依赖 `pixcake_persist` 和 `pixcake_support`，不链接 Qt、OpenGL、SQLite、网络或解码器。
- 依据桌面规格第 3.3 节识别 JPG/JPEG、PNG、TIF/TIFF、BMP 的内容头；扩展名不作为放行依据。
- 已知格式的截断头、非法头、零尺寸和无法安全计算的尺寸返回 `ImageCorrupt`；未知内容返回 `UnsupportedFormat`。
- 文件准入区分 `SourceMissing`、权限错误、读取错误、格式不支持和图片损坏，并保留绝对路径及 `FileIdentity`。
- 模块不写项目库、不复制像素、不生成缩略图；RAW 仍等待解码器配置和实际解码器计划。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-kernel-import-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-import-20261105 -j2
ctest --test-dir /private/tmp/pixcake-kernel-import-20261105 --output-on-failure
```

结果：**37/37 CTest 通过**，包括 `test_source_probe`、`arch_dependencies` 和
`capability_ownership`；`git diff --check` 通过。

## 未覆盖范围

RAW 解码器和扩展名清单下发、真实像素解码、EXIF/XMP、缩略图、项目库批量插入、
系统安全范围书签、远端服务和 UI 导入流程仍未实现。
