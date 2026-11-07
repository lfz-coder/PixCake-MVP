# SCHP ATR 人物/衣物解析 PoC 记录

日期：2026-11-06  
范围：`/private/tmp` 隔离环境；不修改项目源码、不写入生产依赖。

## 来源与版本

- 官方仓库：[PeikeLi/Self-Correction-Human-Parsing](https://github.com/PeikeLi/Self-Correction-Human-Parsing)
- checkout 提交：`eb84c432cc697f494d99662a05f2335eb2f26095`
- 官方代码许可证：仓库 `LICENSE` 标注 MIT。
- 使用的官方 ATR checkpoint：`exp-schp-201908301523-atr.pth`
- 下载来源：官方 README 提供的 Google Drive 链接
- 权重 SHA-256：`e9d7c91ce3b4e7133df56b599fc817b533e3439c5e8d282a59126d2fda339a2a`

## 运行环境

- Python：隔离虚拟环境 `/private/tmp/pixcake-segmentation-venv`
- PyTorch：`2.9.1`
- torchvision：`0.24.1`
- OpenCV：`opencv-python-headless 4.11.0.86`
- 输入：隔离目录中的 `sample.jpg`
- 输出：标签图 `sample.png`，以及带 logits 的 `sample.npy`

官方 extractor 原本面向 CUDA 和 InPlaceABN 扩展。本次只在临时副本中将设备调用改为 CPU，并用 CPU BatchNorm/激活替代扩展；这些改动没有复制到项目。运行成功，产生 `1024x1024` 标签图和 logits 文件。

## 官方 ATR 标签

```text
Background, Hat, Hair, Sunglasses, Upper-clothes, Skirt, Pants,
Dress, Belt, Left-shoe, Right-shoe, Face, Left-leg, Right-leg,
Left-arm, Right-arm, Bag, Scarf
```

这证明 SCHP ATR 可以提供人物、衣物和配饰的候选原始标签，但它没有直接提供项目要求的所有细粒度部位：例如眉毛、眼白、眼瞳、嘴唇、牙齿、口腔、耳朵、手套、首饰等不能从相近标签推断为已支持。适配器必须将这些部位保持 `unsupported`，直到有独立模型或明确的可验证派生规则。

## 结论与限制

1. SCHP ATR 的官方权重已下载并在隔离 CPU 探测环境成功运行。
2. 该结果还不是生产适配：没有接入 C++/`renderd`，没有锁定推理运行时到项目构建，也没有完成权重和训练数据的许可核验（2026-11-06 修订：用户确认 PixCake 不用于商用。按非商用门槛评估）。
3. 下一步只能在许可证、权重来源、运行时后端和标签映射审计完成后，选择明确支持的部位实现真实适配器；没有对应标签的部位必须继续不可用。
