# 锐化与降噪移植证据：PhotoCraft Camera Raw 阶段 8

日期：2026-11-10。来源：PhotoCraft `885a6ddf27cc58c07faf1722e5c327cd38953657`，MIT。

状态：`photocraft-ported`（范围：8 位 sRGB 输入）。

## 参数怎么对应（用户 2026-11-10 授权“按推荐的来”）

官方“细节”分组的项目名来自 2026-11-05 的页面图（[精修滑杆范围](2026-11-05-refine-color-slider-bounds.md)），范围没有实测。对应关系是我的推断：Camera Raw 里只有明亮度降噪带“对比”、只有颜色降噪带“平滑度”。

| 官方 | 参数 | 范围 / 默认 | PhotoCraft |
| --- | --- | --- | --- |
| 锐化 | `sharpen_amount` | 0..150 / 0 | `sharpen_amount` |
| 锐化-半径 | `sharpen_radius` | 0.5..3.0，步进 0.1 / 1.0 | `sharpen_radius` |
| 锐化-细节 | `sharpen_detail` | 0..100 / 25 | `sharpen_detail` |
| 噪点消除 | `noise_luminance` | 0..100 / 0 | 明亮度降噪 `noise_luminance` |
| 噪点消除-细节 | `noise_luminance_detail` | 0..100 / 50 | `noise_luminance_detail` |
| 减少杂色 | `noise_color` | 0..100 / 0 | 颜色降噪 `noise_color` |
| 减少杂色-细节 | `noise_color_detail` | 0..100 / 50 | `noise_color_detail` |

- 范围和默认值取 Camera Raw，也就是 PhotoCraft 的默认值。步进都标为待确认。各项数量为 0 时，它的子项不可调。
- 不登记：
  - 官方的“噪点消除-对比度”“减少杂色-平滑度”：PhotoCraft 没有这两个公式。
  - PhotoCraft 的锐化“蒙版”（`sharpen_masking`）：官方面板没有，固定为 0。移植代码里保留了蒙版的计算，并且有测试。
- “AI 降噪”是 PixCake 的 AI 业务，不在这次范围内。

## 公式

`camera_raw::detail`（`gpu/render/src/camera_raw_spatial.cpp`）逐行移植 `camera_raw.rs` 的 `detail`。先把像素拆成 BT.709 亮度 Y 和色度 B−Y、R−Y，然后：

1. **明亮度降噪**：对 Y 做引导滤波。半径为 round((1+3n)·ps)，eps 为 (0.06n)²+1e-6。细节滑杆在 100 时保留一半残差。
2. **颜色降噪**：对色度做高斯模糊，σ = 6n·ps。细节滑杆保留 0.3 倍残差。
3. **锐化**：对 Y 做 USM。半径先夹到 0.5..3，再乘 ps。细节滑杆限制光晕：残差截断在 ±(0.03+0.3d)，再乘 (0.5+0.5d)。蒙版是平滑后亮度梯度的 smoothstep。
4. 由 Y、B−Y、R−Y 还原 RGB。

位置：细节操作排在所有调色操作之后（顺序号 80 起）。锐化/降噪参数和颗粒用同一个绘制阶段 `detail.grain`，所以一份文档只编译出一个细节操作。在这个操作里先跑阶段 8，再在它的浮点结果上加颗粒（阶段 9），最后按覆盖率回混，与 `develop` 的 8 → 9 顺序一致。

最终审查时发现第一版用了单独的阶段 `detail.sharpen`，结果编译出两个操作，颗粒（80）先画，降噪（87）后画，把颗粒抹掉了。已改为同一阶段，并用 `test_document_detail_before_grain` 从文档编译到像素做了验证。

架构管线里另有一个 `detail.denoise` 步骤，位置在 AI 追色之前。这里的锐化与降噪没有放在那里，而是按 PhotoCraft 放在全部调色之后（用户授权“按推荐的来”）。`detail.denoise` 步骤留给 AI 降噪。

GPU：
- 先用 `kColor`（所有步骤关闭）把当前帧拷进 Rgba32F 平面。
- 在平面上跑 `camera_raw_gl::detail`：Y/色度拆分、GPU 引导滤波、GPU 高斯、边缘蒙版、合成。
- 颗粒绘制从单元 3 读这个结果（`grain3`），回混时仍以原帧为基准。
- 和其他整帧阶段一样，只在一个 GPU 分块之内、且不少于 32 万像素时走 GPU，否则整幅走 reference。

## 证据

| 测试 | 内容 | 结果 |
| --- | --- | --- |
| `test_camera_raw_detail` | 52 组 PhotoCraft develop 向量（`tools/photocraft_vectors detail`）：13 组参数，在 detail、grey、block、strip 四种帧上。参数覆盖锐化默认值、150/半径 3/细节 100、半径 0.5/细节 0、半径超出被夹住、蒙版 50/100、明亮度降噪 60 和 100/细节 0、颜色降噪 50 和 100/细节 100、全开、ps 0.5 和 0.25 | 单线程、多线程都逐位一致（最大差 0） |
| `test_camera_raw_gl` `test_detail_gl` | GPU 阶段对 CPU，6 组参数 × 6 种帧 | 最大差 1.43e-6（容差 1/4096） |
| `test_capability_registry` / `test_capability_modules` | 范围、默认值、禁用规则；`detail` 的参数从 7 个变为 14 个 | 通过 |
| `test_reference_spatial` `test_detail_then_grain` | 一个同时含细节和颗粒的操作：executor 与 CPU 移植“先细节、后颗粒”一致；只开锐化时也会绘制 | 0 级 |
| `test_reference_color_groups` `test_document_detail_before_grain` | 真实文档（颗粒 + 降噪 + 锐化）只编译出一个细节操作；在平坦灰图上颗粒与只开颗粒时逐字节相同（降噪不抹掉颗粒） | 通过 |
| `test_gpu_backend_native` | 锐化 + 降噪 ≤1；调色之后接细节 + 颗粒 0；32 px 分块回退，差 0 | ≤1 级 |
| `test_gpu_no_driver_warnings` | 每种绘制各起一个新进程：调色、蒙版、整帧阶段、颗粒、细节 + 颗粒、图层、曲线都不引起 GL 驱动警告 | 通过 |

变异检查（都会让测试失败）：
- 降噪 eps 系数 0.06 → 0.07
- 蒙版系数 0.08 → 0.09
- 颜色保留 0.3 → 0.31
- GPU 光晕上限 0.3 → 0.31
- GPU 边缘下限 0.5 → 0.6
- 颗粒绘制不读细节结果

## 门禁（移植规范第 8 节）

1. 来源与许可证：`camera_raw_spatial.cpp/.hpp`、`camera_raw_gl.cpp/.hpp` 已登记。满足。
2. CPU 与 PhotoCraft 对照向量逐位一致。满足。
3. GPU readback：真实 context。满足。
4. 阶段级差 ≤1.43e-6，8 位输出 ≤1 级。满足。
5. 预览与导出都用 `RenderExecutor`。满足。
6. 以前没有实现，没有旧公式需要替换。

## 没有做的

- 精修界面还没有这些滑杆。
- 32 万像素的 GPU 门槛是按调色整帧阶段测出来的，没有为细节阶段单独测。
- 官方逐像素一致性未验证。
