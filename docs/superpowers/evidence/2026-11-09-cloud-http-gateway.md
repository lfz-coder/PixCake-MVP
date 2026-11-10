# 云目录 HTTP 网关编排证据

日期：2026-11-09
结论：客户端阶段 2A 已实现并通过专项/回归检查；真实后端、生产 codec/HTTP transport 和云空间页面尚未接通。

本文件记录阶段 2A 验收时的范围。后续已新增独立后端 B1a，见[本地目录服务证据](2026-11-09-cloud-backend-directory.md)；客户端生产对接仍待完成。

依据：[云空间契约 CLD-09–13](../specs/2026-11-09-cloud-space-contract.md)、[客户端计划](../plans/2026-11-09-cloud-space.md)、[后端建设计划](../plans/2026-11-09-cloud-backend.md)。实现来源为 PixCake-specific，无新增第三方运行时。

## 本轮实现

- `services/cloud_transport/include/pixcake/cloud_transport/cloud_wire_codec.hpp` 定义空间/项目分页与协议转换端口；未猜测生产 API、令牌格式或 JSON 字段。测试编码器只存在于测试目录。
- `HttpCloudGateway` 实现既有 `CloudGateway`：空间/项目分页聚合、游标循环和页数限制、单调时钟整体预算、响应大小和累计条目限制、HTTP 状态映射。任何失败都丢弃本次部分结果。
- `HttpRequest` 追加响应正文/头部限额及默认关闭的重定向开关，保留 `send(request, timeout)` 方法。云网关覆盖 codec 的重定向设置，向每次发送传入剩余限额与时间。
- 完整目录仍交 `CloudService` 统一校验；分页失败保留有效目录，鉴权拒绝撤销旧权限，账号失效使迟到结果被拒绝。
- `services/cloud` 的依赖限制不变；只有独立的 `cloud_transport` 能直接引用 net。门禁测试扩展为 2 个合法样例、16 个违规样例。

用户确认没有现成后端，已新增后端计划并明确桌面可直接调用 API，不需要开发浏览器。网页后台是后续可选客户端。

## 测试覆盖

测试入口：`tests/services/cloud_transport/test_http_cloud_gateway.cpp`。以下均为注入时钟、HTTP transport 和类型化 codec 的确定性测试，不发出网络请求。

| 测试函数 | 实际覆盖 |
| --- | --- |
| `test_cld09_cld10_pagination_routes_and_one_budget` | 空间/项目各两页、身份/空间/游标传递、编码与解码耗时计入预算、发送预算依次 490/360ms、限制透传、禁止重定向 |
| `test_cld12_status_and_transport_failures_never_decode` | 401/403 权限拒绝，其余非 2xx 失败；断网/取消/I/O 失败传播；错误响应不解码 |
| `test_cld13_encode_rejection_and_cld12_decode_partial_output` | 编码失败不发送，拒绝非 HTTPS/控制字节/非 GET 或 POST/头部注入；解码错误不泄漏部分结果 |
| `test_cld03_second_page_failure_retains_directory` | 已有目录后刷新第二页失败，上一份目录保留 |
| `test_cld09_cursor_cycles_page_count_and_empty_page` | 循环游标、页数/游标上限、空中间页与合法空目录 |
| `test_cld10_deadlines_include_encoding_transport_and_final_decode` | 编码、传输和最终解码各自耗尽预算、时钟倒退、非法超时和身份/空间参数 |
| `test_cld11_raw_and_cumulative_budgets` | 单页/累计正文、头部字节数/数量、请求正文上限、无效限额；超限在解码前失败 |
| `test_cld11_decoded_total_is_bounded` | 跨页空间总量、单页项目总量超限后不返回部分目录 |
| `test_cld02_cld05_integration_auth_stale_and_bad_semantics` | 贯穿服务的跨页重复 ID、错误项目空间、HTTP 401 撤权、解码期间登出后的旧响应拒绝 |
| `test_cld10_system_clock_constructor` | 默认单调时钟构造路径，仍使用假 transport/codec |

RED：配置后首次构建 `test_http_cloud_gateway` 因缺少 `pixcake/cloud_transport/http_cloud_gateway.hpp` 失败。添加独立目标与实现后 GREEN；未改变测试期望迁就实现。

`test_cloud_dependencies` 验证原目录层不能引用 net/adapter；适配层可引用 net，但不能引用 Qt、具体 HTTP 库、存储或 generated 业务。全局门禁同时禁止 `kernel` 反向依赖云层。

## 可复现命令与结果

项目根目录执行，使用独立构建目录：

```sh
cmake -S . -B build-cloud-space -DCMAKE_BUILD_TYPE=Debug \
  -DPIXCAKE_ENABLE_INFERENCE=OFF -DPIXCAKE_DEV_PAGES=OFF
cmake --build build-cloud-space --target test_http_cloud_gateway test_cloud_service \
  test_account_session test_generation_adapter test_shell_display test_command_gate \
  test_project_session architecture_gate -j4
ctest --test-dir build-cloud-space \
  -R '^(test_http_cloud_gateway|test_cloud_service|test_account_session|test_generation_adapter|test_shell_display|test_command_gate|test_project_session|test_cloud_dependencies|arch_dependencies|capability_ownership)$' \
  --output-on-failure -j2
```

最终结果：`100% tests passed out of 10`，总测试耗时 1.23 秒。所有列出的构建目标成功，`architecture_gate` 输出 `cloud dependencies ok`、`arch dependencies ok`。

`test_generation_adapter` 验证共享 `HttpRequest` 扩展未破坏已有生成适配测试；账号、显示模型、命令入口和项目会话回归通过。云网关测试链接行只有 cloud_transport/cloud/account/support 本地库（net 是接口目标），没有 Qt、SQLite 或具体网络运行时。

范围只包括上述专项和回归，不宣称全仓测试或全产品验收通过。

## 仍需真实实现与验证

- 尚无生产 codec、HTTP/TLS 实现、真实登录服务、数据库或对象存储；本轮不产生云项目或上传文件。
- 同步网关在每个阶段检查时限并传递剩余预算，无法中断不守约的阻塞实现；真实墙钟超时和取消需要 transport 实现及沙盒验证。
- 返回后检查不能撤销 transport/codec 已发生的内存分配；生产实现必须在接收/解压/解析时执行限制。URL/头部完整语法、可信来源与 TLS 验证仍是生产 codec/transport 的责任。
- 现有类只支持串行使用；测试中的回调顺序不代表线程安全。后台请求与 UI 状态交还、页面文案/列表、项目快照/迁移和上传下载尚未实现。
- 分页快照一致性、游标过期和服务端权限变化需要 B0–B1 定义真实协议后联调。
