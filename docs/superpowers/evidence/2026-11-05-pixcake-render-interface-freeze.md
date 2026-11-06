# PixCake 参数与渲染描述接口冻结证据

日期：2026-11-05

## 已完成

- `ParameterMode` 已成为阶段一参数范围、步进、默认值、渲染阶段、稳定程序名、通道名、模式版本和绘制顺序的唯一来源。
- `kernel/render` 新增 `pixcake::render::RenderDescription` 和只读 `compile` 接口。
- 编译器优先读取内存预览值；没有已提交片段时也能生成预览操作，拖动滑杆不需要先写盘。
- 编译器只读取 `EditDocument` 与 `CapabilityRegistry`，不写文件、不改变文档、不创建 OpenGL/Qt/GPU 对象。
- 未注册能力、过新片段和无可编译参数会进入诊断并跳过绘制，原始片段仍保留在文档中。
- `gpu/render` 后续可按 `program`、`stage`、`channel`、`numeric_value` 和 `order` 消费描述；具体像素公式仍属于后续 GPU 实现。

## 验证

构建目录：`/private/tmp/pixcake-render-contract-20261105`

```text
ctest --test-dir /private/tmp/pixcake-render-contract-20261105 --output-on-failure
100% tests passed out of 32
```

其中包括：

- `test_render_description`
- `test_capability_registry`
- `arch_dependencies`
- `capability_ownership`

## 未完成范围

本次只冻结并实现内核侧接口，不宣称完成 `gpu/render` 的 OpenGL/Vulkan/Metal 着色、实时帧生成、远端渲染或导出像素管线。展示线 `apps/shell` 和 `ui` 未作为本次接口改动目标。
