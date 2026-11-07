# 语义分割可分发候选审查：MediaPipe 多类别人像分割与 Apple Vision

日期：2026-11-06  
状态：许可证审查完成；尚未做样片质量验证，未进入生产构建。  
2026-11-06 修订：用户确认 PixCake 不用于商用。本记录按“商用分发”门槛写成；修订后 BiSeNet 与 Sapiens 不再因非商用条款排除（见依赖基线 §2.2），MediaPipe 结论不变。  
背景：BiSeNet（CelebAMask-HQ 非商业条款）和 SCHP（权重/数据产品授权不明）不能发布，见 `2026-11-06-segmentation-label-mapping.md`。本记录按 `2026-11-06-pixcake-third-party-dependency-policy.md` §2 审查两个替代候选。

## 结论

| 候选 | 许可证结论 | 能覆盖的项目部位 | 主要风险 |
|---|---|---|---|
| MediaPipe SelfieMulticlass 256×256 | 模型卡明确 Apache 2.0，可随产品分发（需带许可证文本并标注改动） | 头发、面部皮肤、身体皮肤、全部衣服；派生全部皮肤、全部人物（待样片确认） | 输出仅 256×256；模型卡把“需要像素级精确蒙版”的应用列为不适用；训练数据来源未公开 |
| Apple Vision 人像分割 | 系统框架，不分发权重，无额外授权问题 | 仅全部人物 | 只有整人蒙版，无细分部位；仅 macOS，Windows/鸿蒙需另配方案 |

另查：Meta Sapiens（28 类人体部位，含嘴唇/牙齿/舌头）模型卡为 CC BY-NC 4.0。原按商用门槛排除；修订后可作为候选，需另测模型体积与桌面性能，并按官方原文核对类别。

建议：MediaPipe 作为第一批细分部位的主候选，进入样片质量验证；Apple Vision 作为 macOS 上“全部人物”的对照/备选。两者都不改动现有适配层，只需各自一张映射表。

## 1. MediaPipe SelfieMulticlass

### 来源与锁定

- 官方文档：<https://developers.google.com/edge/mediapipe/solutions/vision/image_segmenter>（模型表 “SelfieMulticlass (256 x 256)”，Quantization: None (float32)）
- 官方模型卡：<https://storage.googleapis.com/mediapipe-assets/Model%20Card%20Multiclass%20Segmentation.pdf>（标题 “Mediapipe Multiclass Segmentation 15/03/2023”）
- 固定版本下载地址（不用 `latest`）：
  `https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_multiclass_256x256/float32/1/selfie_multiclass_256x256.tflite`
- 2026-11-06 实测：文件 16,371,837 字节，SHA-256 `c6748b1253a99067ef71f7e26ca71096cd449baefa8f101900ea23016507e0e0`；与 `latest` 地址下载的文件逐字节相同。GCS `last-modified: Wed, 26 Apr 2023 03:33:37 GMT`，`x-goog-generation: 1682480017063560`。
- 下载文件只在会话临时目录，未提交仓库。

### 许可证

- 模型卡第 1 页 “LICENSED UNDER: Apache License, Version 2.0”；作者 “Adel Ahmadyan, Google”；模型日期 “May 10, 2023”。
- 代码仓库 google-ai-edge/mediapipe 标注 Apache-2.0。
- 分发义务：随包附带 Apache 2.0 全文；若把 `.tflite` 转换为 ONNX/CoreML，转换产物属于修改，需在 NOTICE/依赖记录中注明“由原模型转换”。
- 训练数据：模型卡只写 “Model is trained on thousands of samples with diverse identities”，未公开数据来源或授权。授权方是 Google，许可直接覆盖权重，没有像 CelebAMask-HQ 那样的非商业限制；数据来源不透明属于残余风险，若公司法务要求训练数据溯源，需单独确认。

### 用途声明（与产品的契合度）

- 适用：“Human segmentation from images/videos in interactive entertainment, and video conferencing applications.”
- 不适用：“This model is not intended for human life-critical decisions and applications that require pixel perfect masks”；“Any form of surveillance or identity recognition is explicitly out of scope”。
- 这不是许可证限制，但直接关系到修图导出质量：256×256 概率图放大到全分辨率后，边缘（发丝、衣物边缘）必须单独验证，可能需要导向滤波等边缘细化。这一项进入样片验证门槛。

### 输入输出

- 模型卡：输入为归一化 RGB，256×256（另提到 512×512 大模型，但官方文档只发布 256×256）；输出 6 通道 [0,1] 概率图，由 MediaPipe 后处理取最大值得到类别图。
- 模型内嵌 `labels.txt`（实测提取）：`background`、`hair`、`body-skin`、`face-skin`、`clothes`、`others`，索引 0–5。模型卡附录写作 1–6 且第 6 类为 “others (accessories)”，以模型内嵌标签和 0 起索引为准。
- 公平性：512 模型平均 IoU 81.10；256 模型 77.23，深肤色组（Tone 9+10，31 例）最低 68.25。样本量很小，样片验证需覆盖深肤色。
- 运行时：官方形态为 `.tflite`。已锁定的 ONNX Runtime 1.30.0 不能直接加载，需要二选一：(a) 引入 LiteRT/TFLite C API；(b) 用 tf2onnx 等工具离线转换为 ONNX 并记录转换版本、产物哈希、与原模型的数值对比。

## 2. Apple Vision

依据本机 SDK 头文件（`/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`，macOS 27.2 实机）：

- `VNGeneratePersonSegmentationRequest`：`API_AVAILABLE(macos(12.0), ...)`，“Performs person segmentation on an image generating a mask.” 输出单张整人蒙版，像素格式可选 `OneComponent8`（默认）、`OneComponent16Half`、`OneComponent32Float`。
- `VNGeneratePersonInstanceMaskRequest`：`API_AVAILABLE(macos(14.0), ...)`，“generates an instance mask of individual people found in the image”，输出逐人实例蒙版。
- Vision 没有对任意图片输出头发/皮肤/衣物等细分部位的请求。
- `AVSemanticSegmentationMatte`（Skin、Hair、Teeth、Glasses）：头文件说明是 “captured and stored along with a primary image”，以辅助图像存于图片文件中、通过 `CGImageSourceCopyAuxiliaryDataInfoAtIndex` 读取。只有拍摄时生成过该辅助图的照片才有，不能为任意图片生成，因此不能作为通用来源；以后可作为“照片自带时优先读取”的增强项单独评估。
- 项目目前未设置 `CMAKE_OSX_DEPLOYMENT_TARGET`；采用 Vision 需要先确定最低系统版本（≥12，若用实例蒙版则 ≥14）。

## 3. 下一步门槛

1. 选定 MediaPipe 运行方式（LiteRT 或离线转 ONNX），按依赖规范锁定版本和哈希。
2. 样片验证：至少覆盖单人/多人、半身/全身、深浅肤色、发丝与复杂衣物边缘；确认 `face-skin` 是否包含眉、眼、唇（决定映射到“面部皮肤”还是“面部”），确认 `others` 的实际内容。
3. 全分辨率导出的边缘质量达标后，才把映射表接入 `renderd` 预览。
