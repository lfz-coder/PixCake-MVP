# PixCake 像素助手 Agent sidecar 实现计划

日期：2026-11-10
状态：进行中（阶段 1、2 完成，阶段 3 的本地生命周期与流式入口完成，阶段 4 已接入精修、快照与开发 fixture 的异步生成路径）

## 当前进度

- [x] 迁移并清理 `agent/`，只保留 PixCake 专用 LangGraph、工具白名单、协议和 fixture。
- [x] 冻结请求/工具调用/事件 schema，加入无模型 Python 测试和 SSE 分块测试。
- [x] 建立无 Qt/HTTP/JSON/Python 依赖的 C++ `AssistantGateway`，覆盖确认、修订号、参数边界和幂等提交。
- [x] 建立 C++ `AssistantChannel`，使用现有 Unix socket framing；Python 已提供对应的本地 JSON gateway adapter。
- [x] 增加 C++ SSE parser 和 Qt `AssistantClient`，支持可选 sidecar `QProcess`、非阻塞 JSON/SSE 流和取消。
- [x] 在主进程创建用户级 Unix listener/JSON adapter，并按环境配置启动/回收 sidecar。
- [x] 把助手对话面板绑定到 `AssistantClient::stream`，显示用户消息、SSE 事件、状态和停止操作。
- [x] 在 Qt shell 增加 SSE client 和助手面板状态模型。
- [x] 从 C++ capability registry 向助手上下文暴露只读能力描述、参数范围、步长、枚举值、文本长度和禁用规则。
- [x] 首批精修网关支持同一能力的一组参数预览/提交，并在 C++ 网关强制“预览成功后才能提交”。
- [x] 快照工具复用当前照片的 `PreviewSession` 历史，可创建、确认后应用并返回新的文档修订号。
- [x] 任务历史工具读取现有生成会话的持久化历史，不启动任务、不触碰 provider 数据。
- [x] `run_generation` 校验并快速返回任务引用，后台调用现有 `GenerationSession` / `generated`；同一时刻只允许一个助手生成任务。
- [x] `cancel_generation`、停止按钮和切图取消使用原子信号；进程通道只有 worker 读写。
- [x] 后台校验结果尺寸与校验和，主线程核对图片、编辑实例和修订后落层；过期/取消结果不自动写入图层。
- [x] 活跃任务查询复用提交时的历史快照，结束后读取持久化历史，避免和后台账本读写并发。
- [x] 开发 fixture 显式启用并经过桌面确认；真实供应商缺少有效报价时拒绝提交。
- [x] 提交、撤销、快照应用和生成统一通过 C++ 保存的桌面确认卡片；远端布尔标记不能执行操作。
- [x] 增加只读确认结果查询与最近决定上下文；拒绝/停止清理助手预览，执行前核对编辑实例与修订。
- [x] 修复首页回调生命周期、排序/刷新、根布局及源码门禁；补充进程内 Qt 按钮回归。
- [ ] 完成本轮更新后的真实 Qt/生成子进程回归（自动审批额度限制阻止执行；用户已授权重试，仍被审批系统阻止）。
- [ ] 将人像 AI、真实生成供应商报价/确认、导出和任务历史卡片接入网关。

规格文档：[`2026-11-10-pixcake-agent-sidecar.md`](../specs/2026-11-10-pixcake-agent-sidecar.md)。

## 目标

把 `MM-AniPet` 的可复用 Agent 基础设施迁入 PixCake，形成一个可选的本地 Python sidecar。用户在精修页输入自然语言后，sidecar 负责意图理解、工具编排、预览确认和会话状态；PixCake C++ 内核继续拥有编辑文档、历史、参数校验、像素执行、生成任务和账本。

本计划不把 PixCake 变成一个通用桌面控制 Agent，也不让 Python 直接打开原图、写 SQLite、操作 OpenGL 或提交图层。

## 官方产品行为补充

官方《[像素助手](https://www.pixcakeai.com/guide/233147)》手册（2026-09-15）把它定义成对话式修图入口，而不是单纯的聊天框：

- 支持自然语言修图、模糊修图、多轮连续调整和批量智能修图。
- 复杂请求会拆成任务清单，例如“导入 → AI 挑图 → 筛选 → 智能修图 → 导出”。
- 左侧是会话/任务历史，中间是对话与进度，右侧是实时画布和“精细调整”。
- 导出、AI 挑图、素材选择、裁剪、点数确认等关键步骤使用结构化卡片，而不是让模型自由拼接参数。
- 每个完成任务会生成效果快照；对话结果可以应用回照片参数，继续用专业模式手动调整，并支持撤销/重做。
- 支持文生图、图生图、创意工具和部分创意结果的二次编辑；生成任务必须经过点数确认。
- 偏好设置和“以后都这样做”的记忆只影响默认值，不能绕过 C++ 的确认、账本和参数门禁。
- 官方允许用户在助手模式和专业模式之间切换，并保留当前上下文；助手停止时已完成结果保留，未完成部分终止。

因此 Agent 的输出不只有 `EditPlan`，还包括 `TaskPlan`、`CardRequest`、`SnapshotRef` 和 `ProgressEvent`。`TaskPlan` 负责跨图片/跨模块编排，单个精修操作仍由 C++ 命令和文档提交。

## 冻结的边界

```text
Qt shell / 主进程
  → AssistantClient：发送 JSON，接收 SSE 事件
Python sidecar
  → LangGraph 状态、对话、规划、审批、PixCake 工具适配器
C++ Assistant Gateway
  → 读取上下文、生成预览、校验并提交 BusinessCommand
renderd / generated / exportd
  → 执行预览、生成和导出
```

- Python sidecar 是本地可选进程，开发时可用 LangGraph dev，发布时由 PixCake 按需启动并绑定回环地址。
- Agent 请求只携带图片/编辑实例/选区/文档修订号等引用和低分辨率上下文；不通过 JSON 或 Base64 传输原图像素。
- C++ 是编辑结果的唯一事实来源。Python 的会话状态和模型输出不能代替 `EditDocument`、`LayerDocument`、历史或账本。
- 所有提交都带 `request_id`、`command_id` 和 `base_revision`，C++ 对过期计划、重复提交、越界参数和错误目标拒绝执行。
- 手动滑杆、撤销、预设和普通预览直接走 C++；只有需要语义理解的助手请求走 Agent。
- 未配置或未启动 Agent 时，普通 PixCake 功能必须继续工作。

## 迁移范围

### 保留并改造

- MM-AniPet 的 LangGraph Agent 入口和状态图思路。
- 工具注册、协议/schema 校验、风险级别元数据。
- 错误回传、取消、步数预算和最小循环防护。
- Human-in-the-loop 中断/恢复语义。
- LangGraph Platform 兼容的 JSON 输入和 SSE 输出。
- 会话 `thread_id`，但编辑事实仍保存于 PixCake 文档。

### 删除或置空

- 宠物人格、情绪、贴纸、TTS 和 `PetStatus`。
- 文件、Shell、桌面控制、剪贴板、应用启动和网页工具。
- MM-AniPet 的 Redis/PostgreSQL RAG、文档摄入和知识库依赖；后续若需要官方手册问答，另建只读资料源。
- MCP 动态工具；PixCake 初版使用显式白名单工具，避免外部工具绕过编辑门禁。
- ReAct/Reflect/Supervisor/Orchestrator 通用模式的默认暴露。PixCake 使用固定的“解析 → 规划 → 预览 → 确认 → 提交”状态图；复杂生成任务以后再增加子图。

### PixCake 工具第一批

- `get_edit_context`：读取当前图片、编辑实例、文档修订和已启用能力。
- `get_selection_context`：读取选区、人物/语义目标和蒙版引用。
- `build_edit_plan`：把自然语言转换为版本化、可校验的操作列表。
- `preview_edit_plan`：请求 C++ 生成预览，返回 `preview_id`、诊断和可选费用估算。
- `commit_edit_plan`：在用户确认和修订号匹配后提交业务命令。
- `reject_edit_plan` / `undo_edit`：结束或回滚助手产生的计划。
- `run_generation`：创建 PixCake 生成任务；不直接上传原图或扣点。
- `create_task_plan`：创建可展示的多步任务清单，并声明每步的输入、输出和确认点。
- `request_structured_card`：请求导入、挑图、导出、素材、裁剪或点数确认卡片；卡片值由 UI/C++ 返回。
- `create_snapshot` / `apply_snapshot`：保存或应用一次助手效果快照。
- `list_task_history` / `switch_task`：读取和切换本地助手任务，不让 Python 成为任务正文的唯一存储。
- `get_preferences` / `save_preference`：读取或保存用户偏好默认值；保存必须经过用户明确操作。

## 阶段

### 阶段 1：迁移与协议骨架

- 把上游 `agent/` 复制到项目的 `agent/`，改成 `pixcake_agent` 命名空间和 PixCake 专用 `pyproject.toml`。
- 删除上述无关目录和依赖，建立最小可导入的 Agent 状态、工具定义、工具注册和图入口。
- 冻结 JSON 请求、工具调用和 SSE 事件 schema，并写纯 Python 单元测试。
- 提供无模型的 `plan`/`preview` fixture，使没有 API key 时也能验证协议和错误边界。
- 把官方的任务清单、结构化卡片、效果快照、多轮上下文和助手/专业模式切换纳入 schema。

### 阶段 2：C++ 本地 Assistant Gateway

- 在 `services/assistant` 建立与 Qt 无关的领域接口，使用已有本地进程通道；不把 HTTP/JSON 引入 `kernel/`。
- 实现上下文读取、计划验证、预览请求、提交门闩、取消和状态冲突结果码。
- 为 `request_id`/`command_id`/`base_revision`、重复提交和过期提交增加测试。

### 阶段 3：主进程 sidecar 生命周期与流式客户端

- 主进程按需启动 sidecar，健康检查、超时、取消、崩溃回收和日志脱敏。
- 增加 AssistantClient：JSON POST + SSE 解析；处理 `plan.ready`、`preview.ready`、`approval.required`、`completed`、`error`。
- 不让助手事件阻塞预览、导出或主窗口事件循环。

### 阶段 4：首批精修能力接入

- 从现有 capability registry 生成只读能力清单和参数范围。
- 先接曝光、对比度、色温/色调、局部调整和人像肤色等可回滚能力。
- 每个工具先预览，生成式和会改变文档/点数的操作必须确认。
- 预览与导出继续共用现有 render 描述和执行路径。

当前首批路径已经复用 `PreviewSession::preview`、`commit` 和 `commit_parameters`，因此参数仍由现有
`ParameterMode`/`CommandGate` 校验。人像肤色目前只有 capability module，还没有可写的
`ParameterMode`，继续作为只读能力标识，等生成/检测 handler 就绪后再开放编辑工具。

### 本轮执行：助手生成任务

1. 扩展领域网关与 Python 白名单：异步提交、任务状态和幂等取消。提交时绑定任务 ID 和完整参数；不同请求重放相同任务不会重新生成，改写任务参数会被拒绝。
2. 在生成会话拆开等待/文件校验与图层提交，后台输出只包含引用和元数据的已校验结果。主线程不读全尺寸结果像素。
3. 接入单任务 worker、取消信号、主线程完成回调与会话生命周期。切换照片只发取消信号，退出/注销前回收 worker。
4. 先用显式 fixture 验证连续任务、过期修订、取消、切图和 Qt 事件循环；不把零点 fixture 报价当作真实供应商报价。
5. 运行 Python/C++ 定向测试、实际子进程冒烟和架构门禁，记录当前工作区其他来源门禁问题。

本轮验证：10 项定向 C++/Qt 测试、15 项 Python 测试、架构门禁通过；全局壳层源码门禁存在本轮以外的问题。具体命令、覆盖范围和限制见[验证记录](../evidence/2026-11-10-assistant-generation.md)。

当前生成子进程启动时读取账本快照，因此下一个任务会在后台关闭上一子进程并重新启动，确保读到新报价/入队事件。没有引入驻留推理模型或通用线程池；未来生产适配若需要复用进程，应先提供账本刷新协议。取消不能保证撤销已完成的远端工作；已进入生成阶段的扣点仍由现有账本规则决定。

### 阶段 5：打包与验收

- 开发模式、无 Agent 模式、本地 sidecar 模式和可选远程模式分别验证。
- 测试 sidecar 不可用、模型超时、C++ 崩溃、SSE 中断、重复提交、过期计划、用户拒绝和生成任务取消。
- 记录协议版本、工具版本、命令 ID、文档修订号和脱敏诊断；不记录原图像素、凭证或完整 prompt 中的私人内容。

## 验收标准

1. `cmake --build` 和现有 C++ 测试不受 Python sidecar 影响。
2. `python -m pytest` 在无模型、无网络、无 API key 的 fixture 模式下通过。
3. Agent 不可直接访问 PixCake 项目库、原图文件、OpenGL 或图层文档。
4. 同一 `command_id` 重放不会产生第二次提交；旧 `base_revision` 一定返回状态冲突。
5. 用户不确认时只生成预览，不改变正式编辑文档。
6. sidecar 退出后，PixCake 仍可继续手动修图、预览和导出。


### 本轮执行：桌面确认闭环与当前问题修复

1. 修复壳层源码门禁：文案归并到现有文案表、恢复可读排版；同时修复首页筛选回调捕获栈变量、排序/刷新失效及内容缺少根布局的问题。
2. 领域网关保存完整待执行操作，远端 `user_confirmed` 仅保留兼容输入，不再授予执行权限。增加只能由桌面调用的批准/拒绝入口及只读状态查询；提交、撤销、应用快照和生成统一经过该入口。
3. 精修助手展示由 C++ 内容生成的确认卡片，点击后重新验证照片、编辑实例和修订号；拒绝、停止和切图使待确认操作失效。开发 fixture 明示零点，真实供应商继续关闭至报价契约接入。
4. Python 工具改为申请确认并查询结果，不要求模型自报已获授权；网关状态明确区分待确认与已执行。
5. 补充伪造确认、重复点击、参数替换、拒绝、过期状态和桌面按钮回归；运行源码/架构门禁、Python 和 C++/Qt 定向测试，记录证据与剩余限制。

本轮实现与验证状态见[桌面确认验证记录](../evidence/2026-11-10-assistant-confirmation.md)。完整进程测试仍待环境审批恢复，不能用此前异步生成测试通过替代本轮确认链路的验证。
