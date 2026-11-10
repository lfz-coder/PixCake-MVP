# 校准移植证据：PhotoCraft Camera Raw 阶段 1b

日期：2026-11-10。来源：PhotoCraft `885a6ddf27cc58c07faf1722e5c327cd38953657`，MIT。

状态：`photocraft-ported`（范围：8 位 sRGB 输入）。

## 参数（用户 2026-11-10 授权“按推荐的来”）

官方“校准”分组有红、绿、蓝原色的色相和饱和度六个滑杆（[精修滑杆范围](2026-11-05-refine-color-slider-bounds.md)；名称来自页面图，范围是按滑杆惯例填的 −100..100，没有实测）。PhotoCraft 把这两组值同样夹在 −100..100 之间，所以按它登记在 `color.global` 下：

`calibration_red_hue`、`calibration_red_saturation`、`calibration_green_hue`、`calibration_green_saturation`、`calibration_blue_hue`、`calibration_blue_saturation`：−100..100，默认 0，步进待确认。

PhotoCraft 还有“阴影-色调”（`shadow_tint`），官方面板上没有这一项，所以不登记。

## 公式

`camera_raw::applyCalibration` 逐行移植 PhotoCraft 的 `calibrate` 和 `calibration_weights`：

1. sRGB 解码到线性光。
2. 求 HSV。饱和度 ≤1e-5 或明度 ≤1e-6 的像素原样返回。
3. 按色相求三原色的三角权重：中心在 0°、120°、240°，宽 120°，权重归一。
4. 色相转动：各原色的 Hue 按权重加起来，±100 对应 ±30°。饱和度：各原色的 Saturation 按权重加起来，乘到原饱和度上，并夹在 0..1。
5. 换回 RGB，按 BT.709 亮度保持原亮度。
6. 先取 max(v, 0)，再编码回 sRGB。

位置：白平衡/曝光（阶段 1）之后，紧接着就是校准。之后才是 AI 风格偏移、亮度、对比度。含整帧参数的操作，校准在 GPU 前段和 reference 第 1–3 步里执行，因此在去雾之前，与 `develop` 一致。

GPU：`kColor` 的 `colour_step` 调用 `cr_calibration`，参数用 `cal0`、`cal1` 两个 uniform 传入。

## 证据

| 测试 | 内容 | 结果 |
| --- | --- | --- |
| `test_camera_raw_calibration` | 10 组 PhotoCraft develop 向量（`tools/photocraft_vectors calibration`）：每个原色的色相、饱和度；全开；超出 ±100 被夹住；白平衡与曝光之后；曝光 2 之后。另有 −30°..375° 的权重表 | 最大差 0，权重逐位相同 |
| `test_capability_registry` | 范围；没有阴影色调 | 通过 |
| `test_reference_color_groups` `test_calibration` | executor 与 CPU 移植，含白平衡、对比度 | 0 级 |
| `test_gpu_backend_native` | 校准 0；白平衡 + 曝光 1.5 + 亮度 + HSL 之后 0；整帧 + 校准 0；32 px 分块 0 | 0 级 |

变异检查（都会让测试失败）：转动角度 30° → 31°；权重宽度 120° → 121°；着色器里的转动角度 30° → 33°。

## 门禁（移植规范第 8 节）

1. 来源与许可证：`camera_raw.cpp/.hpp` 的文件头已登记。满足。
2. CPU 与 PhotoCraft 对照向量逐位一致。满足。
3. GPU readback：真实 context。满足。
4. 8 位输出与 reference 差 ≤1 级（实测 0）。满足。
5. 预览与导出都用 `RenderExecutor`。满足。
6. 以前没有实现，没有旧公式需要替换。

精修界面还没有校准滑杆，参数只能经命令或文档写入。官方逐像素一致性未验证。
