# BiSeNet 语义抠图 PoC 记录

日期：2026-11-06  
范围：隔离目录 `/private/tmp/pixcake-segmentation-assets/face-parsing-v0.0.2`；没有把模型、权重或 Python 运行时写入项目生产构建。

## 固定输入和依赖

| 项目 | 值 |
|---|---|
| 上游项目 | `yakhyo/face-parsing` |
| release | `v0.0.2`，commit `5a8613c` |
| 模型 | ResNet18 ONNX |
| 模型文件 | `resnet18.onnx` |
| 模型 SHA-256 | `0d9bd318e46987c3bdbfacae9e2c0f461cae1c6ac6ea6d43bbe541a91727e33f` |
| 输入样片 | 上游仓库 `assets/images/1.jpg`，1024×1024 JPEG |
| 输入 SHA-256 | `18b35dd98b31c0390f04d6bbc0ddaaec6432474679ac0b8454afb7343ecb779d` |
| Runtime | ONNX Runtime `1.30.0`，macOS arm64 wheel |
| Runtime providers | `CoreMLExecutionProvider`, `AzureExecutionProvider`, `CPUExecutionProvider` |
| Python | 3.12.10 |
| 预处理 | RGB，缩放到 512×512，ImageNet mean/std 归一化，NCHW float32 |

官方仓库 README 明确提供 ResNet18/ResNet34 的 ONNX 权重、导出脚本和 ONNX 推理脚本；本次预处理按其 `onnx_inference.py` 复现，没有自行猜测输入布局。

## 实际运行结果

模型输入为 `[1, 3, 512, 512]`，第一个输出为 `[1, 19, 512, 512]`。CPU 推理耗时约 `0.0752 s`；CoreML provider 推理耗时约 `0.0461 s`。CoreML 首次编译在当前沙箱产生 `sandbox_extension_issue_file` 日志，但 session 成功建立并返回了输出。

CPU 与 CoreML 的结果比较：

- 最大 logit 绝对差：`1.811981201171875e-05`；
- argmax 分类掩码差异：`0 / 262144` 像素；
- 因此本次样片上 CPU/CoreML 的离散语义标签完全一致。

本次样片出现的标签：

| 模型标签 | 像素数 | PixCake 当前处理 |
|---|---:|---|
| background | 35,222 | 不作为语义蒙版 |
| skin | 69,574 | 可作为候选 `person.face_skin`，需限定脸部裁剪语义 |
| left eyebrow | 1,760 | 可派生 `person.eyebrow` |
| left eye | 1,163 | 只能记录为眼睛区域候选，不能冒充眼白或眼瞳 |
| left ear | 667 | 可派生 `person.ear` |
| nose | 4,792 | 当前项目没有独立鼻子部位，保持内部候选 |
| mouth | 816 | 不能直接冒充 `person.oral_cavity` 或牙齿 |
| upper lip | 820 | 可派生 `person.lips` 的一部分 |
| lower lip | 1,493 | 可派生 `person.lips` 的一部分 |
| neck | 15,750 | 可作为 `person.neck` |
| cloth | 798 | 只能作为脸部附近衣物候选，不能冒充全身 `clothes.upper` |
| hair | 129,289 | 可作为 `person.hair` |

未在该样片中出现的类别不代表模型永远不支持，只代表本次样片没有产生这些预测标签。

## 许可证边界

- 上游仓库代码标注 MIT；许可证文本尚未复制进 PixCake 仓库。
- 模型权重的单独许可证、训练数据集 CelebAMask-HQ 的分发和商业使用条款，本次没有找到足够的可随产品发布证据。
- 因此该模型目前批准用于隔离 PoC，不批准作为 PixCake 发布包中的生产模型资源。
- 2026-11-06 修订：用户确认 PixCake 不用于商用。CelebAMask-HQ 的非商业条款不再是否决理由；使用时须署名并遵守其研究/教育用途表述。生产化仍需样片验证与打包方案。

## 结论

1. BiSeNet ONNX + ONNX Runtime 的真实推理链已经跑通。
2. CPU 和 CoreML 后端在固定样片上输出一致。
3. 该模型可以作为脸部解析 PoC 候选，但不能覆盖 PixCake 全部 25 个部位。
4. 当前不能把 `eye_white`、`iris`、`teeth`、`oral_cavity`、全身衣物或身体皮肤标记为已支持。
5. 进入生产前仍需完成权重/数据集许可证、模型资源打包、更多样片和跨平台验证。
