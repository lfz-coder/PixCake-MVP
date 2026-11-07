# 语义蒙版第一阶段实施计划

日期：2026-11-06  
状态：执行中  
范围：先完成模型无关的 `renderd` 语义蒙版契约和验证；具体模型、权重、推理运行时在完成依赖审计前不进入生产构建。

## 目标

建立一条不会伪造部位的实现边界，为后续接入人物解析、衣物解析和推理运行时提供可验证接口。第一阶段不训练模型，不下载未锁定权重，不修改 `kernel` 的第三方依赖边界。

## 约束

- 人物和衣物 25 个语义部位必须使用稳定项目 ID。
- 每个部位必须明确为 `supported`、`derived` 或 `unsupported`。
- `supported`/`derived` 必须有模型原始标签来源；`unsupported` 必须有原因。
- 只允许拥有像素的部位提交 alpha 蒙版；未知、尺寸错误、步幅错误或多余蒙版拒绝。
- 推理接口放在 `apps/renderd`，不把模型运行时或图像库带入 `kernel`。
- 失败返回现有 `ResultCode` 和诊断文本，不回退到占位像素或近似部位。

## 步骤

- [x] Step 0：核对需求、架构、第三方依赖政策和现有 renderd/exportd 边界。
- [x] Step 1：记录具体候选方向和许可证审计门槛；不把候选写成已集成依赖。
- [x] Step 2：实现 `apps/renderd` 的模型无关语义蒙版契约、25 部位目录和输出验证。
- [x] Step 3：为契约添加单元测试和架构依赖检查，确认没有模型运行时进入 `kernel`。
- [x] Step 4：在隔离环境实际验证 BiSeNet ResNet18 ONNX + ONNX Runtime 1.30.0，以及 SCHP ATR 官方权重 CPU 探测；记录版本、权重 SHA-256、标签输出、性能和限制。完整记录见 `docs/superpowers/evidence/2026-11-06-segmentation-bisenet-poc.md`、`docs/superpowers/evidence/2026-11-06-segmentation-schp-poc.md`。两者仍未获准进入生产构建。
- [x] Step 5a：完成显式原始标签映射适配器；只允许直接标签或明确并集，缺失标签返回不可用。证据见 `docs/superpowers/evidence/2026-11-06-segmentation-label-mapping.md`。
- [x] Step 5a+：补齐适配器拒绝分支和带行填充原始蒙版的测试；适配器失败时清空输出，校验器检查像素缓冲区大小。
- [x] Step 5a++：完成可分发候选许可证审查。MediaPipe SelfieMulticlass 为 Apache 2.0，可作为主候选；Apple Vision 仅提供整人蒙版，作为 macOS 备选；Sapiens 为 CC BY-NC 4.0（原按商用门槛排除，见下条修订）。见 `docs/superpowers/evidence/2026-11-06-segmentation-license-audit-mediapipe-vision.md`。
- [x] Step 5c：许可门槛修订——用户 2026-11-06 确认项目不用于商用；许可按非商用门槛评估（依赖基线 §2.2）。BiSeNet、Sapiens 不再因非商用条款排除。
- [x] Step 5d：运行时——用户 2026-11-06 选择引入 LiteRT 运行时直接运行官方 `.tflite`（不做离线转换），便于后续扩展和更新模型；ONNX Runtime 继续用于 BiSeNet。先隔离锁定、C++ smoke test，再进生产 CMake。隔离验证已完成：LiteRT v2.3.2 C++ 静态库，MediaPipe 模型与官方结果在 256×256 输入上逐像素一致，XNNPACK 约 32 ms；见 `docs/superpowers/evidence/2026-11-06-segmentation-litert-runtime-and-samples.md`。
- [x] Step 5d+：LiteRT 进生产 CMake——开关 `PIXCAKE_ENABLE_INFERENCE`（默认关闭），源码包与模型按 SHA-256 锁定，TensorFlow 预发布依赖按 commit 锁定并记为接受的风险，16 份依赖许可证配置时自动收集，T-22 与架构规则扩展；`MediaPipeSelfieSegmenter`、`BlazeFaceDetector` 可用，集成测试对照官方通过。见 `docs/superpowers/evidence/2026-11-06-segmentation-litert-integration.md`。
- [x] Step 5f：人脸检测——选定 MediaPipe BlazeFace 全距离版（短距离版在合影中全部漏检）；C++ 后处理（锚框、加权 NMS、信箱还原）与官方在原生输入上分数一致、框差 ≤ 1 像素；检测框 × 2.0 裁剪 + BiSeNet 得到面部皮肤（`skin` + `nose`，用户确认含鼻子）。见 `docs/superpowers/evidence/2026-11-06-segmentation-face-detection.md`。
- [x] Step 5g：ONNX Runtime 1.30.0（官方 macOS arm64 包，SHA-256 锁定）进入生产构建，BiSeNet 面部解析接入 `renderd`；总开关改名 `PIXCAKE_ENABLE_INFERENCE`；`PortraitSegmenter` 支持 10 个部位（新增面部皮肤、全部皮肤、眉毛、嘴唇、耳朵），集成测试与 Python 参考一致。见 `docs/superpowers/evidence/2026-11-06-segmentation-onnxruntime-integration.md`。
- [x] Step 5e：样片验证（第一轮）——`face-skin` 覆盖整张脸（映射“面部”），`others` 为眼镜等配饰；BiSeNet 需人脸裁剪。映射表已更新。缺深肤色、逆光、复杂背景样片，待补。
- [x] Step 5b：真实推理适配器——第一批 MediaPipe（头发、身体皮肤、面部、全部衣服、全部人物），第二批 BiSeNet（面部皮肤、全部皮肤、眉毛、嘴唇、耳朵）。
- [x] Step 6a：接入 renderd 预览服务——部位表移到 `kernel/mask`，语义蒙版编号 `semantic:<部位>`；`SemanticMaskProvider` 按内容缓存分割结果并交给参考渲染；不可用部位跳过并给原因；`renderd` 进程创建 `PortraitSegmenter`。见 `docs/superpowers/evidence/2026-11-06-segmentation-preview-integration.md`。
- [x] Step 6b-1：renderd 请求循环——ImageIO 解码（顺带解除预览帧超过 4000 不能缩放的限制）、套接字 + 共享内存、`pixcake_app_renderd --socket`。见 `docs/superpowers/evidence/2026-11-06-renderd-transport-and-decoder.md`。
- [x] Step 6b-2：壳层启动 renderd、发送预览源与请求、画布显示帧（开发入口 `--open`；顶层色彩滑杆可拖、经命令入口提交；RGB 直方图）。见同一证据 §5。
- [x] Step 6b-2+：正式入口——开发登录下 主页导入 → 新项目 → 精修，图库列出照片、双击进精修，书签解析原图路径。见同一证据 §5.1。
- [x] Step 6b-2++：图库与精修底部的缩略图（renderd 生成，见同一证据 §5.2）。
- [ ] Step 6b-2+++：拖到程序坞图标（需求 3.2）。
- [ ] Step 6b-3：局部调色的蒙版选择在能力片段里写入 `semantic:` 编号（要改能力注册，等 Free 分组参数注册结束）；多蒙版并集语义、羽化/密度/加减。
- [ ] Step 6c：exportd 全分辨率路径；取消、超时、内存上限测试。

## 当前阶段完成定义

当前阶段已完成模型无关契约、隔离模型 PoC、C++ 运行时 smoke test 和显式标签映射适配层。许可门槛已按非商用修订；在运行时锁定和样片验证完成前，真实模型资源和第三方运行时仍不进入生产构建；这不是将缺少部位标记为可用的理由。

## 参考

- `docs/superpowers/specs/2026-11-02-pixcake-desktop-phase1-design.md` §10.4
- `docs/superpowers/specs/2026-11-02-pixcake-kernel-architecture-design.md` §6.6
- `docs/superpowers/specs/2026-11-05-pixcake-render-contract.md`
- `docs/superpowers/specs/2026-11-06-pixcake-third-party-dependency-policy.md`
- `docs/superpowers/evidence/2026-11-06-segmentation-dependency-baseline.md`
