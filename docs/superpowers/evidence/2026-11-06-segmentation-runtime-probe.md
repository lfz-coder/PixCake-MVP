# 语义抠图运行时隔离探测记录

日期：2026-11-06  
范围：`/private/tmp` 隔离环境；不修改项目源码、不写入生产依赖。

## 探测目标

确认当前执行环境是否已经具备运行 ONNX 语义分割候选所需的 Python 运行时和模型文件。

## 当前环境

| 项目 | 结果 |
|---|---|
| Python `onnxruntime` | 未安装 |
| PyTorch | 已在隔离环境安装并用于 SCHP CPU PoC |
| OpenCV | 已在隔离环境安装并用于 SCHP CPU PoC |
| Pillow | 已安装 |
| NumPy | 已安装 |
| 项目内 `.onnx`/`.pt`/`.pth`/`.mlmodel`/`.mlpackage` | 未发现 |

## 网络探测

在 `/private/tmp/pixcake-segmentation-venv` 创建隔离虚拟环境后，尝试安装固定版本 `onnxruntime==1.20.1`：

```text
python3 -m venv /private/tmp/pixcake-segmentation-venv
/private/tmp/pixcake-segmentation-venv/bin/python -m pip install \
  --disable-pip-version-check --no-input --only-binary=:all: \
  onnxruntime==1.20.1
```

结果：失败。当前执行环境无法解析 PyPI 地址：

```text
Failed to establish a new connection: [Errno 8] nodename nor servname provided
No matching distribution found for onnxruntime==1.20.1
```

这不是模型不兼容证据，也不是许可证通过证据；它只证明当前环境没有可用的包下载网络，因此本轮不能运行真实 ONNX 模型。

## 更新：BiSeNet 运行时已实际验证

随后在允许访问上游资源的隔离环境中取得官方 `yakhyo/face-parsing` `v0.0.2` ResNet18 ONNX 权重，并使用 ONNX Runtime `1.30.0` 实际运行。完整结果见 `2026-11-06-segmentation-bisenet-poc.md`。本文件前面的“当前环境没有运行时和权重”描述仅对应第一次探测，不再代表当前隔离 PoC 状态。

## 更新：SCHP ATR 已实际运行

在隔离环境中取得官方 SCHP ATR checkpoint，并以 CPU 兼容探测脚本运行官方 extractor：

- 官方仓库提交：`eb84c432cc697f494d99662a05f2335eb2f26095`。
- 权重来源：官方 README 的 Google Drive ATR 链接。
- 权重 SHA-256：`e9d7c91ce3b4e7133df56b599fc817b533e3439c5e8d282a59126d2fda339a2a`。
- 输出类别：18 类 ATR 标签，包含 `Hat`、`Hair`、`Upper-clothes`、`Skirt`、`Pants`、`Dress`、鞋、脸、四肢、`Bag`、`Scarf`。
- 测试输入：隔离目录中的一张样片；输出为 `1024x1024` 标签图和 logits 文件，推理成功。
- 该次 CPU 运行使用了临时 CPU 兼容补丁（CUDA 调用和 InPlaceABN 扩展未进入项目），因此只证明模型权重、网络结构和标签输出可在当前机器上完成探测，不构成生产实现证据。

## 更新：ONNX Runtime C++ 包已实际运行

- 官方包：`onnxruntime-osx-arm64-1.30.0.tgz`
- 包 SHA-256：`6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012`
- 包内版本：`1.30.0`
- 包内提交：`f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7`
- C++ smoke test 使用官方 C++ API 加载 BiSeNet ResNet18 ONNX，并成功执行 CPU `Session::Run`；输出元素数量为 `4,980,736`，与 `19×512×512` 预期一致。
- 包和 smoke test 都在 `/private/tmp`，没有进入项目源码或生产 CMake。

## 结论

1. `test_segmentation_contract` 验证的是模型适配边界；BiSeNet 的真实运行证据见独立 PoC 记录。
2. BiSeNet ResNet18 ONNX 和 SCHP ATR 都已在隔离环境实际运行；两者的输出标签仍需通过适配器逐项映射到 25 个项目部位。
3. BiSeNet/SCHP 代码、权重和训练数据的许可证仍需按非商用门槛逐项记录（2026-11-06 修订：用户确认 PixCake 不用于商用。），当前只批准隔离 PoC。
4. 在模型标签映射和许可证审计完成前，不能把任何 25 个部位全部标记为真实模型已支持，也不能把临时 CPU 补丁当作生产依赖。
