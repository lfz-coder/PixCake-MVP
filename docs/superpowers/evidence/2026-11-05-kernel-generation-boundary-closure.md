# Kernel 生成任务边界闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-kernel-generation-boundary-closure.md`

## 已完成内容

- `pixcake_generate` 只依赖 `pixcake_support`，不链接 Qt、OpenGL、网络或账本实现。
- 任务描述固定工具编号、参数模式编号、结果形态、计费规则编号、生成张数和可取消标志。
- 结果验收绑定任务编号和结果形态，拒绝未知形态、空标识、越界序号、空校验和和零尺寸。
- 结果可携带供图层文档继续编辑的透明参数载荷；`pixcake_generate` 不解释工具私有格式。
- 状态迁移覆盖已报价、排队中、生成中、成功、失败、排队取消和生成中取消；取消或失败后的晚到结果不能恢复成功。
- 账本仍是扣费和事件持久化的唯一主人；本模块没有任何扣点或远程调用。

## 验证

验证命令：

```text
cmake -S . -B /private/tmp/pixcake-kernel-generation-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-generation-20261105 -j2
ctest --test-dir /private/tmp/pixcake-kernel-generation-20261105 --output-on-failure
```

结果：**36/36 CTest 通过**，包括 `test_generation_task`、`arch_dependencies` 和
`capability_ownership`；`git diff --check` 通过。

## 未覆盖范围

真实远端 AI、网络、配置价格表、生成进程、像素文件写入/解码、图层落层、导入解码、
账号服务、`kernel-cli` 和完整应用进程入口仍未实现。
