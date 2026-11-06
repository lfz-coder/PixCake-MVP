# Kernel 创意配方边界闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-kernel-recipe-boundary-closure.md`

## 已完成内容

- `pixcake_recipe` 保存配方编号、名称、按顺序排列的工具节点和不透明参数快照。
- 节点支持追加、按编号删除和移动；空配方不能开始运行，参数不会被配方模块解释。
- `RecipeRuntime` 维护跨配方的图片占用表，运行期间重复图片返回状态冲突；完成或取消释放占用。
- 配方运行时不依赖 Qt、OpenGL、网络、账本、生成进程或文档写盘。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-kernel-recipe-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-recipe-20261105 -j2
ctest --test-dir /private/tmp/pixcake-kernel-recipe-20261105 --output-on-failure
git diff --check
```

结果：**42/42 CTest 通过**，包括 `test_recipe`、`arch_dependencies` 和
`capability_ownership`；`git diff --check` 通过。

## 未覆盖范围

点数报价、远端生成、结果落层、UI 编辑器、官方配方清单、素材下载和持久化仍未实现。
