# 助手桌面确认与当前问题修复验证

日期：2026-11-10。本文记录本轮工作区验证；不把前一轮生成测试的结果视为本轮确认链路的通过证明。

## 实现范围

- `AssistantGateway` 保存不可由后续 JSON 替换的完整操作。提交、撤销、应用快照和生成先返回 `approval_id` / `pending`，不会调用实际写入 handler。旧 `user_confirmed` 字段仅兼容解析。
- 桌面 `resolve_approval` 执行已保存操作；该接口不在本地 JSON 路由和 Python 工具白名单中。重复点击复用原决定，拒绝/失效不能重新批准；生成在批准前取消时不会入队。
- 精修页展示确认/拒绝按钮。参数沿用已有面板名称，正文是纯文本；本地调整明确不扣点，开发 fixture 明确为测试生成及 0 点，没有生产报价时禁用生成确认。
- 执行时核对照片、编辑实例和修订。撤销还必须对应当前最后一步；拒绝/停止按值恢复助手预览，保留后续用户修改，不写正式文档。
- `get_approval_status` 查询决定；`get_edit_context.approval_history` 返回当前图片/实例/线程最近 16 项决定及命令、快照和任务引用，供后续轮次读取。
- 修复首页回调引用已离开作用域的栈变量、排序/刷新未更新列表、内容没有根布局；文案归并到原有表。全局源码门禁恢复通过。

## 已执行并通过

构建：

```sh
cmake --build build --target pixcake_app_shell test_assistant_gateway test_assistant_events test_generation_history test_generation_result_applier test_edit_document test_shell_sources test_shell_display architecture_gate -j 4
```

定向 CTest：9 项通过。

```sh
ctest --test-dir build -R '^(test_shell_(assistant_ui|app_help|sources|display)|test_assistant_(gateway|events)|test_generation_(history|result_applier)|test_edit_document)$' --output-on-failure
```

| 测试 | 本轮相关覆盖 |
| --- | --- |
| `test_shell_assistant_ui` | 进程内真实 Qt 控件：页面构建返回后的首页搜索/筛选/排序/刷新，确认/拒绝按钮，重复点击，缺报价禁用，纯文本正文；使用 PageActions 测试替身，不启动子进程、不创建 socket 或照片书签 |
| `test_assistant_gateway` | 模型布尔标记无法执行、预览对应性、保存参数不可替换、桌面决定幂等、拒绝/失效、批准前取消、结果上下文查询 |
| `test_edit_document` | 恢复被拒预览不写文档、不改变修订；后续用户参数不同则保留 |
| `test_assistant_events` | SSE 分块解析回归 |
| `test_generation_history` / `test_generation_result_applier` | 既有持久化历史与结果落层边界回归 |
| `test_shell_display` / `test_shell_app_help` / `test_shell_sources` | 显示模型、入口和全局源码约束 |

Python：18 项通过，无模型、无网络 fixture；没有据此宣称真实 LangChain 模型服务通过联调。

```sh
PYTHONPATH=agent/src python3 -m unittest discover -s agent/tests -v
git diff --check
```

架构门禁通过，包含内核依赖和云依赖检查；`git diff --check` 通过。

## 已编译但本轮未能执行的联调

`test_shell_assistant_generation` 已补充：真实本地 JSON 伪造确认、拒绝远端批准命令、Qt 按钮到 C++ 控制器、确认前没有生成/落层、拒绝恢复预览、重复点击、过期撤销/快照、停止撤销待确认，以及原有异步完成/取消/切图测试。

该测试需要沙箱外的本机 Unix socket、临时照片书签和 renderd/generated 子进程。本轮两次自动审批均因使用额度用尽而无法完成，命令没有执行；第二次是在用户明确允许重试后发起。系统明确表示这不是对操作安全性的否定。没有绕过审批，也没有将受阻测试计为通过。

在审批恢复或由用户在本机终端运行后，应记录实际结果：

```sh
ctest --test-dir build -R '^(test_shell_assistant_generation|test_assistant_channel|test_generation_(session|channel))$' --output-on-failure
```

## 后续范围

- 完成上述真实控制器/子进程回归，并按结果修复；本轮尚不能宣称完整集成验收。
- 助手生成入口仍只开放开发 fixture；生产供应商的报价与账本授权契约、语义选区 adapter、通用导出/历史卡片以及 sidecar 打包未在本轮实现。
- 待确认决定保存在主进程内存，单次会话有数量上限；崩溃后不会自动恢复或执行未确认操作。

## 提交前的独立版本检查

为避免依赖工作区中未提交的账号、云同步等代码，按文件和 diff 片段暂存助手、生成链路及首页修复，再从 Git 基线和暂存区导出独立源码目录。独立目录配置为 Debug、开发页面开启、推理关闭，不包含云服务实现。此版本补齐了助手冒烟测试对 `account_session.hpp` 的显式依赖。

- 独立源码构建 shell、生成/渲染/导出依赖及定向测试成功，架构门禁通过。
- 15 项 CTest 通过：原有 9 项，以及生成工具目录、供应商 adapter、generated、renderd、exportd 和导出队列回归。
- Python 18 项测试通过；暂存区 whitespace 检查通过。
- 上文需要额外权限的完整子进程联调仍未计为通过。

复核使用的是暂存区内容，未将工作区剩余的账号、云同步、标记工具及镜头处理的未提交改动混入本次提交。
