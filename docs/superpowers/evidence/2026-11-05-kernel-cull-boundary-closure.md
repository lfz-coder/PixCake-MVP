# Kernel AI 挑图边界闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-kernel-cull-boundary-closure.md`

## 已完成内容

- `pixcake_cull` 保存需求第 15 节十种场景、弱/标准/强检测档位和星标/色标/旗标目标。
- 检测器按排序顺序提交图片决策、重复分组和已有标签事实；会话拒绝空/重复图片编号。
- 入选数量上限只保留前 N 个入选决策；启用保留已有标签时跳过对应目标的写入动作。
- 完成返回决策与标记动作，取消清空未完成决策；模块不运行检测模型、不写项目库、不改精修文档。
- 模块保持无 Qt、无 OpenGL、无网络、无模型运行时依赖。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-kernel-cull-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-cull-20261105 -j2
ctest --test-dir /private/tmp/pixcake-kernel-cull-20261105 --output-on-failure
git diff --check
```

结果：**46/46 CTest 通过**，包括 `test_cull_session`、`test_renderd`、`test_exportd`、
`arch_dependencies` 和 `capability_ownership`；`git diff --check` 通过。

## 额外构建修复

- `gpu/render/CMakeLists.txt` 补上 `pixcake_render` 契约库依赖，修复干净构建时公共头找不到
  `pixcake/render/render_description.hpp` 的问题。
- `apps/renderd/src/render_service.cpp` 的模式校验已使用显式括号，避免 `-Werror` 下的逻辑运算符
  优先级警告。

## 未覆盖范围

人脸/表情/美学/曝光/模糊/重复检测模型、项目库写入、UI、进程通信和精修参数仍未实现。
