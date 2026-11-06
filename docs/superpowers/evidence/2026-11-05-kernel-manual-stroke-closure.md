# Kernel 手动笔触会话闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-kernel-manual-stroke-closure.md`

## 已完成内容

- `pixcake_manual` 定义液化、污点修复、修补、仿制图章、涂抹擦回和祛边的稳定笔触类型。
- `StrokeSession` 绑定图片编号和基准修订号，拒绝空标识、跨图片、过期修订、重复编号、空点集和非有限坐标。
- 未确认笔触只存于打开会话；确认返回完整 `ManualCommit` 快照并关闭会话，取消清空会话且不调用 `EditDocument`。
- 修复、修补和仿制图章没有源点时拒绝；重放失败通过 `mark_replay_invalid` 标记，原始点、源点和笔触编号仍然保留，重复标记幂等。
- 模块只依赖 `pixcake_support`，不依赖 Qt、OpenGL、网络、文档写盘或像素处理。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-kernel-manual-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-manual-20261105 -j2
ctest --test-dir /private/tmp/pixcake-kernel-manual-20261105 --output-on-failure
git diff --check
```

结果：**39/39 CTest 通过**，包括 `test_manual_stroke_session`、`arch_dependencies` 和
`capability_ownership`；`git diff --check` 通过。

## 未覆盖范围

真实液化/修复/祛边算法、GPU/OpenGL 着色器、UI 模态层、文档写盘、历史记录、账本扣费
和实际 `manual.*` 绘制描述仍未实现。
