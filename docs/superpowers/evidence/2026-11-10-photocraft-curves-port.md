# 曲线移植证据：PhotoCraft Camera Raw 第 5 阶段

日期：2026-11-10。来源：PhotoCraft `885a6ddf27cc58c07faf1722e5c327cd38953657`，MIT。

状态：`photocraft-ported`（范围：8 位 sRGB 输入）。

## 参数怎么登记（用户 2026-11-10 授权“按推荐的来”）

官方面板的曲线分组有参数曲线（高光、亮调、暗调、阴影），以及 RGB、亮度、红、绿、蓝通道曲线（[曲线分组实测记录](2026-11-06-free-group-curve-measurement.md)）。官方范围、步进都没有实测，所以按 PhotoCraft 的定义登记在 `color.global` 下：

| 参数 | 类型 | 范围 / 格式 | PhotoCraft |
| --- | --- | --- | --- |
| `curve_highlights`、`curve_lights`、`curve_darks`、`curve_shadows` | 数值滑杆 | −100..100，默认 0，步进待确认 | `curve_highlights` … `curve_shadows` |
| `curve_points_rgb`、`curve_points_red`、`curve_points_green`、`curve_points_blue` | 文本 | `"x,y;x,y;..."`，坐标 0..255，2 到 16 个点，输入按顺序至少递增 1 级 | `point_curve`、`point_curve_red/green/blue`，`validate_curve` |

- 分割点不登记，固定为 PhotoCraft 默认的 25/50/75。官方面板没有分割点控件。
- 线性曲线就是不写这个参数。对角线上的点曲线和不足两个点的曲线都算线性（PhotoCraft `is_linear`）。
- 官方的“亮度通道曲线”在 Camera Raw 里没有对应项，所以不登记，状态是 `no-equivalent`。
- 不合格的点曲线文本在能力层直接拒收（`validate_parameter_display_value`），这一步停用。如果绕过能力层传给 executor，按线性处理。
- 文件：`kernel/capability/include/pixcake/capability/curve_points.hpp`、`kernel/capability/src/curve_points.cpp`、`kernel/capability/src/capability_registry.cpp`。

## 公式

`gpu/render/src/camera_raw.cpp`，逐行移植 `camera_raw.rs`：

- `parametricLut`（`parametric_lut`）：分割点先夹到 5..95。四个区域各有一个高斯凸起，乘以 `x(1−x)·4`，滑杆 ±100 最多移动四分之一个范围。结果裁到 0..1，再保持单调。
- `curveLut`（`curve_lut`）：点除以 255，按输入稳定排序，输入相差不到 1e-6 的点只保留第一个。然后做单调三次插值（Fritsch–Carlson：τ = 3/√(a²+b²)），结果裁到 0..1。
- `lutEval`（`lut_eval`）：0..1 以内线性插值，0..1 以外线性外推。
- 每张表 1024 项（`CURVE_LUT`）。顺序是参数曲线 → master 曲线 → 红、绿、蓝各自的曲线。

在 executor 中的位置：Presence（阶段 4）→ 色阶 → 曲线（阶段 5）→ Color Mixer / 黑白（阶段 6）→ 可选颜色 → Color Grading（阶段 7）。Camera Raw 的 4-5-6 顺序不变。色阶不属于 Camera Raw，按官方面板的顺序放在曲线之前。

- **整帧操作**：曲线在 `kColor` 后段（reference 是 `draw_spatial` 的第 5–6 步），作用在整帧阶段的结果上，与 PhotoCraft `develop` 一致。
- **GPU**：五张表放在一张 1024×5 的 R32F 纹理里，占单元 4（先上传，再上传蒙版，最后绑定）。`kColor` 的 `tone_curves`/`curve_eval` 用 `texelFetch` 移植 `lut_eval`，与 PhotoCraft WGSL 的做法相同。
- **快速路径**：曲线逐通道作用，所以只含 exposure、brightness、RGB 色阶和曲线的全局操作仍走 256 级查表快速路径，与通用路径逐字节相同。

## 证据

| 测试 | 内容 | 结果 |
| --- | --- | --- |
| `test_camera_raw_curves` | 16 组 PhotoCraft 向量（`tools/photocraft_vectors curves`）。三种参数曲线（含分割点和被夹住的分割点）；master 曲线的 S 形、抬黑、平段、陡段、反相、内点、乱序；分通道；全开；曝光 1.5 之后（值超过 1，走线性外推）；Presence + 曲线 + Color Mixer | 表逐项相同（0 项不同），显影最大差 0 |
| `test_capability_registry` | 范围、格式、`validate_curve` 的各种拒收情况（单点、间隔不到 1 级、乱序、超出 0..255、NaN、缺坐标、多余分号、空格、17 个点），16 个点可以收 | 通过 |
| `test_capability_modules` | 点曲线带着文本进入绘制描述；不合格时这一步停用 | 通过 |
| `test_reference_color_groups` `test_curves` | executor 与 CPU 移植一致；对角线曲线和单点文本不改变像素；快速路径与通用路径相同 | 0 级 |
| `test_gpu_backend_native` | 曲线 0；曝光 2 + 曲线 0；曲线 + 色阶 + HSL + 颜色分级 0；整帧 + 曲线 ≤1；32 px 分块 0；`color.local` 带曲线时回退 reference，差 0 | ≤1 级 |

变异检查：

- 会让测试失败：凸起宽度 1.2 → 1.25；τ 的 3 → 2.9；着色器里红色行读成蓝色行。
- 不会让测试失败：插值下标多取半格（`int(x + 0.5)`）。它在 8 位输出上差不到 1 级，在门禁 4 的容差内。

## 门禁（移植规范第 8 节）

1. 来源与许可证：`camera_raw.cpp/.hpp`、`curve_points.cpp/.hpp` 的文件头都有登记，并列入 `check_photocraft_license.cmake`。满足。
2. CPU 与 PhotoCraft 对照向量：表和显影都逐位一致。满足。
3. GPU：`kColor` 在真实 context 上 readback。满足。
4. CPU 与 OpenGL 误差：8 位输出 ≤1 级。满足。
5. 预览与导出执行同一公式：renderd 和 exportd 共用 `RenderExecutor`。满足。
6. 旧公式“旧 → 新”：曲线以前没有实现（`not-implemented`），没有旧公式需要替换。

## 没有做的

- 精修界面的曲线编辑器（拖点、通道切换）还没有。现在曲线参数只能经命令或文档写入。界面由壳层那边接入参数模式。
- 官方逐像素一致性没有验证（`unverified`）。官方范围也没有实测。
