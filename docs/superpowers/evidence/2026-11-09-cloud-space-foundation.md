# 云空间基础边界证据

日期：2026-11-09
结论：云空间阶段 1（目录服务和会话边界）通过本轮专项验收；整个云空间仍未完成。

本文件记录阶段 1 当时的验收范围；后续阶段 2A 的网关编排、扩展门禁与 10 项回归结果见[HTTP 网关证据](2026-11-09-cloud-http-gateway.md)。

依据：[模块契约 CLD-01–08](../specs/2026-11-09-cloud-space-contract.md)、[实现计划](../plans/2026-11-09-cloud-space.md)。实现来源为 PixCake-specific，无新增第三方依赖。

## 交付内容

- `services/cloud`：普通 C++17 的空间/项目摘要类型、`CloudSession`、`CloudGateway` 和只读 `CloudService`，只链接 account/support。
- `AccountSession::generation()`：成功登录、缓存加载和显式退出推进生命周期；偏好修改与失败登录/缓存加载保持不变。2026-11-10 增补：已退出时再次退出也使后台恢复失效，返回值仍为 `StateConflict`。账号会话不可复制，防止绕过生命周期。
- 云会话引用当前账号；退出、同凭证重登和切换账号都立即撤销旧目录的可见性和授权检查。请求返回后再次校验，拒绝旧响应。
- 读/写/管理权限独立，不推导角色继承；服务端权限拒绝时清空旧权限。普通网络错误保留有效目录。
- 限额、重复标识、枚举、字符串、负数及跨空间数据校验；整批通过才返回/替换。
- 请求超时预算传给网关；服务诊断通过 `LogSink` 记录事件/结果码，不带身份或远端文本。
- 专用云依赖门禁加入已有构建门禁；测试含真实的违规头文件/链接负样例。

阶段 1 验收时没有生产 `CloudGateway` 实现。fake gateway 仅在测试目录中，不创建真实云项目、不发出网络请求，不改变本地数据库或云标识。

## 自动测试与对应边界

| 测试函数/目标 | 实际覆盖 |
| --- | --- |
| `test_cld01_s25_live_account_and_logout` | 未登录不调用网关；登出隐藏目录；相同凭证重登不恢复旧授权；关闭云会话不退出桌面账号 |
| `test_cld02_permissions_are_explicit_and_revoked` | 个人读写、团队只读、管理不隐含读取、未知/无权限空间拒绝、服务端拒绝撤销权限 |
| `test_cld03_c19_b50_remote_errors_and_atomic_directory` | NetworkFailed/Cancelled/IoFailed 传播，超时预算透传，坏空间数据/重复 ID 不覆盖旧目录，成功空目录 |
| `test_cld04_s06_projects_validate_entire_reply` | 项目所属空间、ID/名称/数值/重复标识校验；坏返回不暴露部分项目；成功空目录和零值摘要 |
| `test_cld05_s25_stale_responses_and_newer_refresh` | 登出重登、换账号、更新目录和云会话关闭重开期间的旧响应均拒绝 |
| `test_cld06_s06_limits_and_timeout_contract` | 1/60000ms 和越界超时；非法 ID；1024 空间、10000 项目边界及多一项；字符串刚好达限和超限 |
| `test_cld07_o01_s22_logs_contain_only_outcomes` | 服务操作记录最终结果；日志没有身份、会话句柄、项目名或任意网关内容 |
| `test_s25_authentication_generation` | 成功/失败登录、登出、缓存加载与偏好变更对代数的影响；禁止复制/移动赋值 |
| `test_cloud_dependencies` | 1 个合法样例、8 个违规样例：Qt/SQLite/文档/渲染/平台/HTTP 头和 Qt/存储链接 |

测试扩展时的 RED：新增账号代数测试因 `AccountSession` 缺少 `generation()` 编译失败；扩展云测试因缺少 live-session 构造、timeout 参数和限额定义编译失败。实现后进入 GREEN。

第一次独立回归中，全局门禁误扫描了位于构建目录中的 Qt 负样例；将测试夹具移到现有扫描规则排除的 `tests/build/` 后，专用负样例与全局门禁并行通过。未放宽源码依赖禁令。

## 可复现命令与最终结果

在项目根目录执行，使用独立目录，关闭模型构建和开发直达页面：

```sh
cmake -S . -B build-cloud-space -DCMAKE_BUILD_TYPE=Debug \
  -DPIXCAKE_ENABLE_INFERENCE=OFF -DPIXCAKE_DEV_PAGES=OFF
cmake --build build-cloud-space --target test_cloud_service test_account_session \
  test_shell_display test_command_gate test_project_session architecture_gate -j4
ctest --test-dir build-cloud-space \
  -R '^(test_cloud_service|test_account_session|test_shell_display|test_command_gate|test_project_session|test_cloud_dependencies|arch_dependencies|capability_ownership)$' \
  --output-on-failure -j2
```

最终输出：`100% tests passed out of 8`，总测试耗时 1.14 秒。构建门禁输出 `cloud dependencies ok` 和 `arch dependencies ok`。`test_cloud_service` 链接行仅含 cloud/account/support 三个本地库，没有 Qt、网络或 SQLite 运行时。`git diff --check` 通过。

范围内回归：`test_account_session`、`test_shell_display`、`test_command_gate`、`test_project_session`；没有宣称全仓测试或全产品七关验收通过。

## 尚未验收的能力

- 真实 HTTP、codec、认证和分页；实际墙钟超时中断、取消网络调用。当前只验证网关契约和预算传递。
- 上传/下载、持久化传输队列、断点续传、项目绑定迁移与快照格式、冲突处理。
- Qt 云空间页面、传输列表和生产进程日志接线；现有入口行为保持本轮之前状态。
- 多线程并发：现有类要求和账号服务在同一串行执行上下文使用；迟到回调测试不代表线程安全。

后续阶段按计划继续推进。正式网络对接仍需要明确后端及授权接口契约，不能用测试替身结果代替服务端联调。
