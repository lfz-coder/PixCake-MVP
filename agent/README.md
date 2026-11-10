# PixCake Agent sidecar

这是像素助手的可选本地 Python 编排进程。它从 MM-AniPet 的 Agent 目录迁移了通用的 LangGraph/工具编排思路，但只保留 PixCake 需要的自然语言修图边界。

Python Agent 只负责：

- 解析自然语言和多轮上下文；
- 生成任务清单、结构化卡片和编辑计划；
- 请求 C++ 网关生成预览；
- 在用户确认后请求 C++ 提交；
- 通过 LangGraph/SSE 报告进度、审批和错误。

Python Agent 不负责：

- 打开原图或保存正式编辑文档；
- 修改 SQLite、图层文档、历史或点数账本；
- 执行 OpenGL、全分辨率渲染或导出；
- 暴露文件、Shell、桌面控制、MCP 或第三方工具。

## 本地运行

```bash
cd agent
python -m venv .venv
. .venv/bin/activate
pip install -e '.[dev]'
cp .env.example .env
langgraph dev --port 27461
```

默认建议先使用 `PIXCAKE_AGENT_FIXTURE=1` 做无模型、无网络协议测试。连接真实 PixCake C++ 网关时改为 `0`。发布模式优先设置 `PIXCAKE_GATEWAY_SOCKET`，通过 PixCake 创建的 0600 Unix socket 发送一条有长度前缀的 JSON 命令并接收一条回复；开发时也可以设置 `PIXCAKE_GATEWAY_URL` 使用本机 HTTP 网关。

PixCake 主进程通过以下环境变量管理 sidecar 和流式入口：

- `PIXCAKE_ASSISTANT_EXECUTABLE`：可选的 sidecar 可执行文件；为空时由开发环境自行启动 Agent。
- `PIXCAKE_ASSISTANT_ARGS`、`PIXCAKE_ASSISTANT_WORKDIR`：sidecar 参数和工作目录。
- `PIXCAKE_ASSISTANT_URL`：Qt shell 发送请求的 LangGraph SSE URL，默认 `http://127.0.0.1:27461/threads/{thread_id}/runs/stream`；`{thread_id}` 会替换为当前助手线程。
- `PIXCAKE_ASSISTANT_ID`：LangGraph graph/assistant id，默认 `assistant`。
- `PIXCAKE_ASSISTANT_SOCKET`：主进程监听的用户级 Unix socket 路径；未设置时，配置了 `PIXCAKE_ASSISTANT_EXECUTABLE` 的主进程会在用户 runtime/temp 目录自动生成本次进程专属路径，并注入给 sidecar。

当前精修页的助手入口会显示用户消息、SSE 事件、状态和停止按钮；真正的编辑提交仍由 C++ Assistant Gateway 的确认与修订门闩处理。

`get_edit_context` 返回的 `payload` 还包含 C++ capability registry 的只读元数据：
`capability_descriptors` 描述能力标签和批量限制，`parameter_descriptors` 描述数值范围、步长、默认值、枚举项、文本长度、版本和禁用规则。Agent 可以据此生成计划；这些值不是 Python 的权限，预览和提交仍由 C++ 的 `ParameterMode` 与 `CommandGate` 再次校验。

## 输入边界

请求包含 `request_id`、`thread_id`、图片/选区引用、`base_revision` 和用户消息。图片像素不通过 JSON 发送；预览通过 PixCake 的现有渲染通道返回引用。

本地 socket 的每一帧是 `u32 little-endian length + UTF-8 JSON`，上限为 256 KiB；一条连接只处理一个请求和一个回复。C++ Assistant Gateway 只验证命令、修订号、确认门闩和幂等 ID，正式编辑仍由主进程的命令入口提交。

请求、工具调用和事件的版本化约束分别见 `schema/assistant_request.schema.json`、`schema/assistant_tool_call.schema.json` 和 `schema/assistant_events.schema.json`。

## 异步生成

`run_generation(task)` 先返回 `confirmation_required`、`approval_id` 和 `approval_status=pending`，此时没有启动生成。用户在精修页确认卡片后，通过 `get_approval_status(approval_id)` 获取决定；只有结果包含 `task_status=queued` 才表示任务已接收。通过 `list_task_history` 查询后续状态，通过 `cancel_generation(task_id)` 请求取消。取消回复中的 `cancelling` 表示请求已发出，最终结果仍以 C++ 状态为准。

`task` 包含 `task_id`、`tool_id`、`image_id`、`source_revision`、可选参数对象 `parameters`、`mask_ids`、`reference_image_ids` 和 `idempotency_key`。同一任务 ID 的重复提交复用首次结果；换参数重用 ID 会被拒绝。C++ 只接收引用，不接收原图字节。

当前 shell 只开放显式启用的 `creative.fixture` 开发工具。确认卡片由 C++ 保存的完整请求生成，开发测试明确显示 0 点；真实供应商未获得有效报价时禁用确认。任务等待与像素校验在后台，图层写入前重新核对当前编辑实例和修订号。切图、继续编辑或取消都不会自动把过期结果应用到照片。

## 桌面确认契约

`commit_edit_plan`、`undo_edit`、`apply_snapshot` 与 `run_generation` 都只申请确认。模型不能批准，JSON 中保留兼容的 `user_confirmed` 字段也不授予权限。`resolve_approval` 只存在于可信 C++ 桌面 API，不在 socket 路由和工具白名单中。

确认结果有 `pending`、`executing`、`approved`、`rejected`、`failed`、`expired`；批准是单次操作，失败或拒绝后应重新读取上下文、重新规划。`get_edit_context.approval_history` 提供当前图片/编辑实例/线程最近 16 次决定及命令、快照、任务引用，供后续轮次继续使用。确认前后编辑实例或修订发生变化时，主进程拒绝旧操作。生成的最终完成状态仍由任务历史提供。

## 来源

迁移来源和删改范围见 [`NOTICE.md`](NOTICE.md) 与仓库计划 [`docs/superpowers/plans/2026-11-10-pixcake-agent-sidecar.md`](../docs/superpowers/plans/2026-11-10-pixcake-agent-sidecar.md)。
