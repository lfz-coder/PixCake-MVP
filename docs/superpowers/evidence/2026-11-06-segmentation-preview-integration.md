# 语义蒙版接入 renderd 预览

日期：2026-11-06  
状态：`renderd` 预览服务已能按渲染描述里的语义蒙版编号运行分割并参与出图；界面和内核还没有产生语义蒙版编号的入口，`renderd` 进程也还没有请求循环（见 §6）。

## 1. 蒙版编号与部位表

- 部位表移到 `kernel/mask`（`pixcake/mask/semantic_parts.hpp`），按架构 §6.6“语义部位的名称和包含关系是这一个模块里的数据表”。25 个部位、编号与名称和需求 10.4 一致。
- 语义蒙版编号：`semantic:<部位编号>`，例如 `semantic:person.face_skin`。`semantic_mask_id`、`is_semantic_mask_id`、`semantic_part_of_mask_id` 只认表内部位。
- `renderd` 的 `semantic_part_catalog()` 改为从内核表按顺序生成（编号和名称不再在 renderd 重复一份）；两边数量不一致时目录为空，所有校验都会失败，而不是贴错部位。
- `kernel/mask` 只有数据，不链接任何推理运行时（架构规则 15/16 已覆盖）。

## 2. renderd 预览路径

| 组件 | 行为 |
|---|---|
| `SemanticMaskProvider` | 从启用的操作里收集 `semantic:` 编号（去重、保序）；对当前预览帧运行分割器；按图片编号 + 尺寸 + 像素内容哈希缓存最近一次结果，拖动滑杆时不重复分割；把部位蒙版转成执行器的 `RenderMask`（行距压紧） |
| 不可用的部位 | 不给蒙版，诊断写明“<部位名>不可用（编号）：模型给出的原因”；执行器把引用它的操作标为缺少资源并跳过——不用其他部位冒充（需求 10.4） |
| 未知编号 | 诊断“未知的语义部位” |
| 分割失败 | 诊断写明结果码和原因，相关操作跳过，其余操作照常出图 |
| 未构建推理 | 诊断“语义分割未构建，语义蒙版不可用”；`--health` 同样报告 |
| `RenderService` | 有启用操作时改用执行器的参考渲染（`renderReference`，CPU；renderd 按架构规则 5b 不用 OpenGL）；预览结果新增逐操作状态和诊断。原先“有启用操作就返回 UnsupportedFormat”的行为随之取消，`test_renderd` 已更新 |
| `renderd` 进程 | 打开推理时创建 `PortraitSegmenter` 交给 `RenderService`；模型目录先找 `.app/Contents/Resources/models`，开发构建退回配置时下载校验的目录 |

预览在最长边 ≤ 4000 的预览帧上分割，导出将在全分辨率上用同一套规则（需求 10.4）。

## 3. 测试

- `test_semantic_parts`：25 个部位、编号唯一、编号前缀规则。
- `test_semantic_mask_provider`（假分割器，无需模型）：蒙版只作用于对应像素；同一图复用分割、像素变化重新分割；不支持的部位跳过并给原因；未知编号；分割失败；未构建推理；无语义蒙版时不运行分割器；带行填充的输入与蒙版。
- `test_renderd`：启用操作出图并报告状态；未注册的程序报告 `Unregistered` 和原因。
- `test_portrait_inference` 端到端：真实三模型 + `RenderService`，对 `semantic:person.face_skin` 局部提亮——面颊变亮，背景和西装像素完全不变；同图第二帧复用分割、结果逐字节相同；`semantic:person.teeth` 画面不变并给出“牙齿不可用”。
- 全量：`PIXCAKE_ENABLE_INFERENCE=ON` 79/79，默认 77/77。

## 4. 耗时（820×1024，Apple Silicon，4 线程）

| 构建 | 首帧（含分割） | 同图后续帧 | 部位不可用的帧 |
|---|---|---|---|
| 未指定构建类型（项目代码 -O0） | 2008 ms | 1518 ms | 48 ms |
| `-O2`（Release，保留断言） | 316 ms | 134 ms | 5 ms |

`-O2` 下单独分割（三模型）约 190 ms，只在首帧发生；后续帧的 134 ms 主要是执行器参考渲染的逐像素循环和内容哈希。测量用 `-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE=-O2`：项目测试依赖 `assert` 执行被测调用，带 `-DNDEBUG` 的 Release 会把这些调用一起编译掉，不能用于测量或验证。

## 5. 与需求的差距

1. 一个操作引用多个蒙版时，执行器把权重相乘（交集）。需求 10.4 的“联动调整”“多个蒙版含同一子蒙版时效果叠加”需要并集/叠加语义，由执行器或蒙版合成决定，本次未改 `gpu/render`。
2. 蒙版羽化、密度、加减、可见性还没有作用到语义蒙版上。
3. 部位包含关系（如“全部衣服包含上衣、下装…”）尚未写进 `kernel/mask` 的表。
4. ~~预览帧超过 4000 时 renderd 仍返回“缩放后端未注册”~~：经 `PreviewServer` 的预览由 ImageIO 在解码时缩到请求的最长边（见 `2026-11-06-renderd-transport-and-decoder.md`）；直接调用 `RenderService` 并传入超大帧时仍拒绝。

## 6. 尚未打通的环节

- 界面/内核：还没有能力在步骤里写入 `semantic:` 编号（局部调色的蒙版选择尚未实现）。
- 进程：~~renderd 没有请求循环~~ 已有（`--socket`，见 `2026-11-06-renderd-transport-and-decoder.md`）；壳层还没有启动 renderd、发送预览请求和显示帧。
- 因此目前是 renderd 预览服务层面接通，真实界面上看到效果还需要上面两步。
