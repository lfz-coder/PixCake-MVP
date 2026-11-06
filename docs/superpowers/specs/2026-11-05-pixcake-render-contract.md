# PixCake 参数与渲染描述接口冻结

## 目的

本接口把“参数是什么”和“如何把参数交给渲染器”固定下来。它不实现白平衡、曝光或任何像素公式，也不携带 OpenGL、Qt、纹理、着色器或窗口对象。`gpu/render` 和未来的导出实现只消费这份描述；主进程和项目库只负责文档与命令。

公共代码位置：

- 参数模式：`kernel/capability/include/pixcake/capability/capability_registry.hpp`
- 绘制描述：`kernel/render/include/pixcake/render/render_description.hpp`
- 只读编译器：`kernel/render/src/render_description.cpp`
- 预览协调器：`kernel/preview/include/pixcake/preview/preview_coordinator.hpp`
- 预览内容协议：`kernel/ipc/include/pixcake/ipc/preview_protocol.hpp`

## 参数模式

`ParameterMode` 是参数范围、步进、默认值和渲染映射的唯一来源：

```text
capability_id
parameter_id
minimum / maximum / step / default_value
render_stage
render_program
render_channel
schema_version
order
```

规则：

1. 用户文件保存 `display_value`，不保存 GPU 整数、纹理编号或后端对象。
2. 正式提交只能使用已有模式；未知参数、越界值和错误步进拒绝提交。
3. `render_program` 是稳定的业务程序名，例如 `pixcake.color.global`，不是 GLSL 源码或 OpenGL 对象名。
4. `render_channel` 是渲染器使用的通道名，例如 `temperature`、`tint`、`exposure`。
5. `order` 使用能力管线的稳定拓扑顺序，决定预览和导出的绘制顺序。渲染器不得自行按界面出现顺序重排。
6. 参数模式升级通过 `schema_version` 处理；不能把过新片段静默改写或删除。

阶段一已冻结的模式包括：全局/局部/后期色彩、AI 追色和风格程度、蒙版羽化、裁剪角度、眼神光位置/旋转和光影均匀明度。参数模式可以先于绘制注册表发布范围，但默认 `CapabilityRegistry` 只会编译已经注册为绘制能力的片段；因此尚未注册的模式会被保留并给出诊断，不会被伪造为已完成的绘制能力。没有正式范围的能力仍不能伪造范围。

## RenderDescription

`pixcake::render::RenderDescription` 是只读数据：

```text
schema_version
image_id
editing_instance_id
document_revision
operations[]
diagnostics[]
```

每个 `RenderOperation` 包含：

```text
capability_id
stage
program
order
values[]
mask_ids[]
blend_mode
enabled
disabled_reason
```

每个 `RenderValue` 包含参数编号、稳定通道名、用户显示值和经过验证的数值。`mask_ids`、`blend_mode` 和禁用原因先作为协议字段冻结；当前内核没有完整蒙版/图层绘制实现时保持为空或给出诊断，不用另一个能力冒充。

`RenderDescription::layers` 和 `selection` 只描述合成输入：图层顺序、父子关系、标准混合模式、透明度、隐藏状态、蒙版密度/羽化和选择区羽化。它们不携带像素、纹理、着色器或窗口对象。未知混合模式、坏父子关系和越界值会留下禁用图层与阻塞诊断。

## 编译规则

`pixcake::render::compile`：

- 只读 `EditDocument` 和 `CapabilityRegistry`，不改文档、不写文件、不创建 GPU 对象。
- 对每个参数优先读取内存预览值，未设置预览值时读取已提交值；只有预览值时也可以生成操作，因此拖动滑杆不要求先写盘。
- `PreviewCoordinator` 只保留一个待处理请求，新请求替换旧请求；请求绑定原图、编辑实例和基础修订号，`take_latest` 交付时再次检查这些身份。
- 按 `ParameterMode::order` 排序操作，预览和导出可复用同一描述。
- 未注册能力、过新片段和没有可编译参数的片段保留在原文档中，并写入诊断后跳过绘制。
- 文档恢复为空或缺少原图编号时返回明确结果码。
- 描述版本目前为 `1`。变更字段时提升版本并增加兼容测试。

## 进程边界

```text
主进程 / apps/shell
  → 参数命令和预览副本
kernel/render
  → RenderDescription（无 Qt、无 OpenGL）
apps/renderd / apps/exportd
  → gpu/render 消费描述
gpu/backend/qt 或 gpu/backend/native
  → 具体 GPU API
```

`apps/shell` 不提交着色器，`kernel` 不链接 GPU 或推理运行时，`gpu/render` 不写项目库和参数文档。
