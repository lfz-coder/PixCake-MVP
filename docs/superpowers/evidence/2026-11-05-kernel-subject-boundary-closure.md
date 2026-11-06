# Kernel 人物检测与属性边界闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-kernel-subject-boundary-closure.md`

## 已完成内容

- `pixcake_subject` 保存 `Pending`、`Ready`、`Failed` 检测状态，以及已验证的人脸框、稳定人脸编号和可选同一人编号。
- 未完成或失败的检测不暴露任何人脸几何；`Ready` 允许空人脸列表表示本图没有可用人脸。
- 重新检测只替换可重建的人脸缓存，按稳定人脸编号保留属性覆盖和单人调整锁；模块不猜测跨照片身份。
- 年龄/性别覆盖只能写入已完成且未被单人调整锁定的人脸；恢复原人物属性会清掉覆盖和单人锁。
- 模块只依赖 `pixcake_support`，不依赖 Qt、OpenGL、网络、模型运行时或文档写盘。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-kernel-subject-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-subject-20261105 -j2
ctest --test-dir /private/tmp/pixcake-kernel-subject-20261105 --output-on-failure
git diff --check
```

结果：**40/40 CTest 通过**，包括 `test_subject_store`、`arch_dependencies` 和
`capability_ownership`；`git diff --check` 通过。

## 未覆盖范围

真实人脸/属性模型、跨照片识别算法、年龄性别 UI、批量同步、像素渲染、文档写盘和人物
能力参数编译仍未实现。
