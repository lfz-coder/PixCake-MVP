# 官方 AI 服务接入材料核查

日期：2026-11-18  
状态：已取得官方产品文档、官方测试环境地址和客户端内的协议线索；官方开发者 API 文档、可复现的认证流程以及真实脱敏请求/响应样例仍未公开取得。

## 1. 核查范围与证据来源

本记录只使用以下两类材料：

1. 像素蛋糕官方公开页面和官方知识库；
2. 本机安装的官方测试客户端 `/Applications/pixcake-test.app` 的只读静态资源检查。

客户端身份已核验：

| 项目 | 值 |
|---|---|
| Bundle ID | `com.xiangtian.pixcakepc.test` |
| 版本 | `9.3.0 (811)` |
| 主程序 | `Contents/MacOS/pixcake` |
| 测试环境提示 | 客户端包含 `test`/`dev` 环境切换文案 |

静态检查没有读取本机账号数据库、令牌、原图、请求抓包或日志中的敏感内容，也没有向 AI 生成接口提交任务。

## 2. 官方公开文档

### 2.1 产品能力与用户侧语义

- [官方 AI 工具箱说明](https://www.pixcakeai.com/guide/230427)：确认 AI 工具箱是开放型 AI 工具聚合平台，工具由创作者提供并接入创意模块；工具可能带参数、滑杆和提示词；生成存在排队、加速、取消和进度；部分工具返回“细节图层”和“效果图层”。
- [官方知识库 AI 工具箱总览](https://wiki.pixcakeai.com/ai-toolbox)：确认工具包、作者和按场景组织的工具目录。
- [官方计费说明](https://wiki.pixcakeai.com/guide/9630.html)：确认免费排队、加速生成、创意点扣除、批量生成和重新生成的计费语义。
- [官方工作区说明](https://wiki.pixcakeai.com/guide/9379.html)：确认 AI 工具箱位于创意页面，并与侧栏、图层面板共同工作。

这些页面没有公开开发者 API 的 base URL、版本化 OpenAPI/SDK、认证字段、测试账号、请求 JSON、响应 JSON、上传/轮询/取消/下载协议或错误码表。

### 2.2 官方客户端确认的测试地址

在官方测试客户端主程序的静态字符串中观察到以下测试环境主机：

| 用途线索 | 地址 | 证据级别 |
|---|---|---|
| 核心 API | `https://api-test.pixcakeai.com` | 官方测试客户端内置地址 |
| 用户 API | `https://pixuser-api-test.pixcakeai.com` | 官方测试客户端内置地址 |
| Web 编辑器 API | `https://web-editor-api-test.pixcakeai.com` | 官方测试客户端内置地址 |
| Agent API | `https://agent-test.pixcakeai.com` | 官方测试客户端内置地址 |
| 推送长连接 | `wss://push-agent-api-test.pixcakeai.com/v1/api/ws` | 官方测试客户端内置地址 |
| 消息 API | `https://message-api-test.pixcakeai.com` | 官方测试客户端内置地址 |

本项目目前只把 `https://api-test.pixcakeai.com` 记录为候选核心 API 地址；其他主机的具体职责必须由官方协议材料确认，不能据此擅自拼接请求。

## 3. AI 路由线索

官方测试客户端主程序中出现了以下 AI/Comfy 路由字符串。它们是客户端实现线索，不等同于已经公开授权的 API 合同：

### 3.1 官方 AI 工具路由

```text
/aigc/basic_info
/aigc/generate
/aigc/task/generate
/aigc/task/batch_generate
/aigc/task/status
/aigc/task/clean
/aigc/feedback
/aigc/report
/app/user/aigc_points
```

### 3.2 Comfy/创作者工具路由

```text
/comfy/tools/page
/comfy/workflows/recommend
/comfy/workflows/detail
/comfy/authors/hot
/comfy/task/generate
/comfy/task/free_generate
/comfy/task/batch_generate
/comfy/task/status
/comfy/task/clean
/comfy/task/free_status
/comfy/task/free_cancel
/comfy/task/switch_to_paid
```

从客户端内部日志字符串还能确认它存在“生成后轮询状态、长连接超时后切换轮询、下载 OSS 结果”的流程线索；这只能确认交互形态，不能确认 HTTP 方法、字段名或状态值。

## 4. 认证线索与边界

客户端静态资源中同时出现了以下认证相关字符串：

```text
Authorization
Bearer
AuthorizationType
X-Request-Id
X-Request-Timestamp
/token/info
/user/sts_token
/user/get_channel_token
/user/get_global_refresh_channel_token
/user/jump_token
```

因此可以确认：客户端请求层存在 `Authorization`/`Bearer` 语义，并使用请求 ID、请求时间戳以及用户/STS/channel token 相关接口。当前仍不能安全确认：

- `Authorization` 的完整值格式、令牌类型和有效期；
- 是否还需要签名、nonce、设备指纹或其他必填头；
- `/user/sts_token` 等接口的请求体、响应体和换取顺序；
- AI 路由使用用户 token、STS token、channel token 还是组合认证；
- 免费队列/加速队列是否使用不同的授权或幂等字段。

客户端还包含 `Idempotency-Key`、`Content-Type: application/json`、`multipart/form-data`、`Left-Aigc-Points` 等字符串；这些字符串尚不足以证明它们在每个 AI 路由上的位置和必填性。

## 5. 字段线索与脱敏样例状态

静态字符串中能观察到部分任务/结果词汇：

```text
requestId
taskId / taskUUID / task_uuid
httpTaskId
aigcType
generateCount
imgWidth / imgHeight
prompt
mask
formulaParams
status / progress
errorCode / errorMessage
resultData
ossPath
resultFilePath
```

这些名称只能作为适配器字段映射的待确认清单。当前没有从官方授权测试调用中取得一份可复现的真实请求/响应，因此不能把下面这种结构写成官方协议：

```json
{
  "request": "<official redacted request not yet supplied>",
  "response": "<official redacted response not yet supplied>",
  "status": "unavailable"
}
```

项目现有的 `FixtureWireCodec` 仍是本地开发替身，故意使用 `fixture://` 和 `application/x-pixcake-fixture`，不冒充上面的官方路由。

### 5.1 客户端嵌入的 schema 级样例

官方测试客户端还内置了 `apply_creative`/`image_to_image` 和 `text_to_image` 工具的 JSON Schema 片段。它们不是网络抓包，但可以作为已脱敏的类型级参考：

```jsonc
// apply_creative / image_to_image：客户端 schema 级样例，值均为占位符
{
  "project_id": 123,
  "image_ids": [456],
  "creativeCfg": [
    {
      "aigcType": 41,
      "prompt": "<redacted>",
      "imgWidth": 1024,
      "imgHeight": 1024,
      "generateCount": 1
    }
  ],
  "retry": false,
  "trace_id": "<redacted>"
}
```

该片段的必填约束是 `project_id`、`image_ids`、`creativeCfg`；`image_ids` 是整数数组，`creativeCfg` 是非空数组，`retry` 默认为 `false`。客户端 schema 中对应的结果对象包含以下字段：

```json
{
  "results": [
    {
      "origin_image_id": 456,
      "new_image_id": 789,
      "image_id": 789,
      "queued": true,
      "task_id": "<redacted>",
      "queue_task_id": "<redacted>",
      "run_id": "<redacted>",
      "error_code": "<redacted>",
      "message": "<redacted>"
    }
  ]
}
```

`text_to_image` 的另一段 schema 只要求 `project_id` 和 `creativeCfg`，并额外声明 `generate_count` 为最小值 1 的整数；其结果同样包含 `image_id`、`queued`、`task_id`、`queue_task_id`、`run_id`、`error_code` 和 `message`。这些字段仍未绑定到一个已确认的 HTTP URL，也没有真实返回值、认证头或 HTTP 状态码，因此只能进入适配器的待确认 schema，不能直接作为生产请求。

## 6. 对项目实现的影响

现有 provider-neutral 主链路已经为以下替换点留好边界：

- `services/net`：真实 HTTP/TLS 传输、超时和取消；
- `GenerationWireCodec`：请求编码、响应解码、错误映射；
- `HttpRemoteTool`：编排传输和 codec；
- `GenerationTask`/`RemoteReply`：保持项目内部稳定，不泄漏供应商 JSON；
- `GenerationSession`/账本/图层落层：继续复用，不由外部协议直接改写。

拿到官方材料后，首个真实适配器需要补齐：

1. 认证令牌获取与刷新；
2. 图片/蒙版上传和 OSS 路径交换；
3. 工具目录与参数 schema；
4. 生成提交、免费/加速模式和幂等键；
5. 状态轮询或 WebSocket 推送；
6. 取消、清理、结果下载和双图层映射；
7. 错误码、创意点报价/扣除/退款语义；
8. 使用官方脱敏样例建立 codec 契约测试。

## 7. 下一步材料清单

要把“线索”升级为“可接入协议”，还需要官方或授权测试方提供：

| 材料 | 最小内容 |
|---|---|
| 开发者文档 | API 版本、base URL、HTTP 方法、Content-Type、超时和限流 |
| 认证说明 | 测试账号/沙盒 token 获取方式、刷新、权限和脱敏规则 |
| 工具目录 | tool/package ID、参数 schema、输入能力、结果形状和点数规则 |
| 请求样例 | 上传、提交、状态、取消、结果下载各一份脱敏请求 |
| 响应样例 | 成功、排队中、生成中、取消、点数不足、鉴权失败、参数错误各一份脱敏响应 |
| 实时通道 | WebSocket/SSE 地址、订阅消息、断线重连和轮询回退 |
| 结果协议 | OSS URL/临时凭证、双图层命名、尺寸、格式和校验方式 |

在这些材料到达前，不实现真实 HTTP 客户端、不提交凭证、不上传原图，也不根据字符串猜测生产 JSON。
