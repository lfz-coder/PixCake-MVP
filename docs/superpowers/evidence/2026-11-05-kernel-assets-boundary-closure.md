# Kernel 素材索引边界闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-kernel-assets-boundary-closure.md`

## 已完成内容

- `pixcake_assets` 支持样片、CUBE、天空、证件照背景、布景和水印六类素材，保存编号、分类、来源路径和可用性。
- 样片单次导入严格限制 1000 张，总量严格限制 2000 张；超限和重复请求整体拒绝，不静默截断或部分写入。
- 文件缺失只把记录标记为不可用，素材编号和分类仍可读取；恢复文件时可重新标记可用。
- 模块只依赖 `pixcake_support`，不依赖 Qt、OpenGL、网络、像素解码或数据库。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-kernel-assets-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-assets-20261105 -j2
ctest --test-dir /private/tmp/pixcake-kernel-assets-20261105 --output-on-failure
git diff --check
```

结果：**43/43 CTest 通过**，包括 `test_asset_catalog`、`arch_dependencies` 和
`capability_ownership`；`git diff --check` 通过。

## 未覆盖范围

素材文件下载、缩略图、远程同步、像素解码、UI 管理、精修文档写回和素材实际渲染仍未实现。
