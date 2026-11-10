# 助手异步生成验证

日期：2026-11-10

## 本轮实现

- Python 工具和 C++ Gateway 支持 `run_generation`、`cancel_generation`、`list_task_history`。提交立即返回任务引用；状态查询区分进行中与终态。
- 任务 ID 绑定完整输入，跨请求重复提交不会重新执行，修改参数/重复参数名/过期修订/错误图片会被拒绝。取消可以重放。
- Shell 使用一个有界并发的后台 worker。生成等待、结果尺寸和校验和检查在后台进行，取消只跨线程传递原子信号，进程通道由 worker 单独读写。
- `VerifiedGenerationResult` 只携带经过验证的引用和元数据。主线程检查照片、编辑实例和修订后执行图层提交，不再次读取全尺寸结果像素。
- 切换照片发出取消信号且不等待任务；旧结果不会写入新会话。注销、重新绑定生成会话和析构前回收 worker，失效完成回调不访问控制器。
- 当前仅开放显式启用的开发 fixture。真实供应商的报价、可信 UI 费用确认和生产打包未完成。

## 执行结果

`cmake --build build --target pixcake_app_shell test_generated test_generation_channel test_generation_result_applier test_generation_session test_generation_history test_assistant_gateway -j 4`：通过。

定向 CTest：**10/10 通过**。

| 测试 | 覆盖 |
| --- | --- |
| `test_shell_assistant_generation` | 隔离临时项目、实际 renderd/generated；立即返回 queued、Qt 定时器持续运行、连续任务、参数变化后拒绝落层、取消与切图后图层字节不变、历史保留 |
| `test_shell_app_help` | 壳层正常启动、开发测试入口可构建 |
| `test_assistant_gateway` | 确认、预览、幂等、生成输入绑定、错误目标/修订、取消重放 |
| `test_assistant_channel` / `test_assistant_events` | 本地 framing 和 SSE 分块解析 |
| `test_generation_channel` | 原子取消信号、生成中取消不退款、晚到结果丢弃、提交前取消不发消息 |
| `test_generation_session` | 入队前取消无副作用、子进程启动失败进入持久化终态、崩溃状态恢复 |
| `test_generation_result_applier` | 校验失败/过期来源不写图层；已校验结果落层不再次读取像素；无效校验凭证被拒绝 |
| `test_generation_history` / `test_generated` | 持久化历史与生成进程现有回归 |

命令：

```sh
ctest --test-dir build -R '^(test_shell_(assistant_generation|app_help)|test_assistant_(gateway|channel|events)|test_generation_(session|channel|history|result_applier)|test_generated)$' --output-on-failure
PYTHONPATH=agent/src python3 -m unittest discover -s agent/tests -v
cmake --build build --target architecture_gate
git diff --check
```

Python：**15/15 通过**；架构门禁和 diff 检查通过。整链路冒烟在受限沙盒中遇到 `bind/listen: Operation not permitted`，随后经执行权限审批在放宽沙盒环境运行通过；所有图片、项目库、账本和图层均位于测试生成的临时目录，无外网模型/供应商请求。

## 未计为通过的检查与边界

- 当前共享工作区的 `test_shell_sources` 报告 22 项问题，涉及 `home_page.cpp` 的行长/硬编码文案、`local_projects_page.cpp` 的文案，以及现有 UI/工作区的重复文案。本轮新增的助手相关壳层代码未出现在这些失败中；未将全局源码门禁或全量 CTest 记为通过。
- 这是开发 fixture 的功能与事件循环回归，不是生产供应商的吞吐量或延迟基准。
- 当前同一时刻只执行一个助手生成任务，重复提交复用原结果，其他新任务返回忙状态。没有新增通用队列或驻留模型。
- 生成子进程读取启动时的账本快照；下一任务由后台先结束旧进程再启动新进程，以读取新的报价和入队事件。生产进程复用需要先增加账本刷新协议。
- `succeeded-source-changed` / `succeeded-not-applied` 等本次会话应用状态只表示未自动落层；重启后的任务状态仍以持久化生成历史/账本为准。真实报价、UI 卡片确认、持久化应用状态与 sidecar 打包仍在后续计划中。

## 后续更新

本记录对应异步生成阶段的历史验证。后续已补桌面确认卡片并修复源码门禁；最新范围、通过项和受环境阻止的重跑项见[桌面确认验证记录](2026-11-10-assistant-confirmation.md)。
