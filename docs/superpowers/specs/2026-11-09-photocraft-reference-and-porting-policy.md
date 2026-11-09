# PhotoCraft 能力来源与 PixCake 移植规范

日期：2026-11-09（同日复核修订：补入 Camera Raw 来源族、拆分来源/状态两轴、修正许可证与参考副本管理）
状态：当前项目绑定决策；新会话必须先读
适用范围：所有像素处理、图层合成、GPU pass、CPU reference、渲染测试、相关需求和实施计划

关联文档：

- [第一阶段需求](2026-11-02-pixcake-desktop-phase1-design.md)
- [架构设计规范](2026-11-02-pixcake-kernel-architecture-design.md)
- [参数与渲染描述接口冻结](2026-11-05-pixcake-render-contract.md)
- [像素处理来源与共识登记](2026-11-06-pixel-processing-provenance.md)
- [第三方依赖规范](2026-11-06-pixcake-third-party-dependency-policy.md)
- [像素处理来源与执行现状矩阵](../evidence/2026-11-09-pixel-processing-status-matrix.md)
- [像素公式与来源逐参数登记](../evidence/2026-11-09-pixel-formula-registry.md)

## 1. 一句话规则

**PhotoCraft 已经实现的影像能力，PhotoCraft 的实现、公式、边界行为和测试就是 PixCake 的首选行为来源；PixCake 必须在自己的 C++17 与 OpenGL 架构中尽可能完整移植这些能力。PhotoCraft 的 Rust、wgpu、egui、crate 分层和进程形状不改变 PixCake 的架构规范。**

“使用 PhotoCraft”在本项目中指使用它已经验证过的能力和行为，并把它移植到 PixCake；不是把 PhotoCraft 的 Rust/wgpu 应用直接嵌入 PixCake，也不是继续把 PixCake 当前的经验公式当作同等权威的第二套实现。

**对应关系按语义判定，不按名字判定。** PixCake 精修的基础调色滑杆（色温、色调、曝光、对比度、高光、阴影、白色、黑色、纹理、清晰度、去雾、鲜艳度、饱和度、HSL 八色带、色彩分级、颗粒）是 Camera Raw/Lightroom 式的显影滑杆，对应 PhotoCraft 的 **Camera Raw 滤镜管线**（`crates/algo/src/camera_raw*`），而不是同名的 Photoshop 调整图层（`crates/compose/src/adjust*`）。例如 PixCake 的“曝光”对应 Camera Raw 的线性光 `2^EV` 增益，不是 Photoshop“曝光度”调整图层的 exposure/offset/gamma；PixCake 的 HSL 八色带对应 Camera Raw 的 Color Mixer，不是 Photoshop“色相/饱和度”的六个范围。

## 2. 四个权威边界

同一件事按下面的分工判断，不能把一种权威扩展到另一种范围：

| 要回答的问题 | 唯一优先来源 | 说明 |
| --- | --- | --- |
| 用户看见什么功能、名称、范围、交互和商业规则 | PixCake 第一阶段需求、固定版本的像素蛋糕行为证据 | PhotoCraft 不决定 PixCake 的产品入口、点数、项目、账号、AI 任务或页面结构 |
| 已有通用像素能力应采用什么行为和公式 | PhotoCraft 对应的 CPU 实现（Camera Raw 管线或 compose 调整/合成）、GPU shader、lowering 和 parity 测试 | PhotoCraft 代码是可移植的参考实现；需要记录版本/提交，不写成 Adobe 官方内部公式 |
| 模块放在哪里、谁依赖谁、如何跨进程和如何使用 GPU | PixCake 架构规范、RenderDescription 契约和依赖规范 | PixCake 保留 C++17/CMake、Qt shell、renderd/exportd/generated、`pixcake::gl` 和 OpenGL backend |
| PhotoCraft 没有覆盖的项目能力 | PixCake 自己的需求、公开标准和明确的工程决策 | 来源标为 `PixCake-specific` 或 `no-equivalent`，不能为填表而伪造 PhotoCraft 来源 |

因此，PhotoCraft 的算法来源优先级高于 `gpu/render/src/render_executor.cpp` 中现有的暂定公式；PixCake 的架构和用户需求优先级高于 PhotoCraft 的应用结构。参数范围、默认值、滑杆刻度仍以 PixCake `ParameterMode` 和像素蛋糕黑盒证据为准；两者单位不同时，必须在公式登记里写出显式换算。

## 3. 两条独立的状态轴

“行为来源”和“移植状态”是两个问题，必须分两列记录，不能用一个词同时回答。

### 3.1 行为来源（这个能力应当照谁做）

| 来源 | 含义 |
| --- | --- |
| `photocraft-target` | PhotoCraft 有语义等价的实现；PixCake 以它为目标行为 |
| `PixCake-specific` | PhotoCraft 没有等价能力，或 PixCake 的产品语义必须不同；按 PixCake 需求、公开标准或工程决策实现，单独记录公式和证据 |
| `no-equivalent` | 没有可采用的 PhotoCraft 等价能力，也没有足够的 PixCake 公式/资源；保留诊断或未实现状态，不写占位效果 |

一个参数可以是“`photocraft-target` + PixCake 扩展”，例如 Camera Raw 颗粒只有数量/大小/粗糙度，PixCake 额外的阴影/中间调/高光/彩色颗粒属于 `PixCake-specific` 扩展。此时必须逐子参数写清哪部分有 PhotoCraft 目标。

### 3.2 移植状态（PixCake 现在跑的是什么）

| 状态 | 含义 | 下一步 |
| --- | --- | --- |
| `legacy-provisional` | PixCake 旧实现仍在工作区，与 PhotoCraft 目标尚未对齐 | 只能作为迁移临时实现和回退测试，不能作为规范或验收基准 |
| `photocraft-reference-only` | PhotoCraft 语义已移植为 PixCake C++ CPU reference，并有固定向量；OpenGL 或预览/导出尚未完成 parity | 不得声称 GPU 或端到端完成 |
| `photocraft-ported` | C++ reference 与 OpenGL 都已移植，并满足第 8 节全部门禁 | 保持来源和提交记录，继续补齐真实图片/色彩管理验证 |
| `not-implemented` | PixCake 没有可执行实现 | 保留诊断，不写占位像素 |

`PixCake-specific` 来源的能力不使用 `photocraft-*` 状态，只记录 `legacy-provisional`（尚未定稿的旧公式）、`not-implemented` 或在证据里写明的 PixCake 验收状态。

“reference 有代码”“GPU 与本项目 reference 一致”“与 PhotoCraft 一致”“与像素蛋糕官方一致”是四个不同结论。当前旧 reference 与 shader 的 readback 一致只证明第二条。

## 4. PhotoCraft 源码与 PixCake 的映射

当前仓库中的参考副本是 `third_party/photocraft`（上游 `https://github.com/storytold/photocraft`，`main`），当前浅克隆提交为 `885a6ddf27cc58c07faf1722e5c327cd38953657`（2026-11-09）。锁定和获取方式见第 10 节。凡是将 PhotoCraft 作为来源的变更，都要在证据或代码注释中写出相对路径和提交；更新副本后重新核对所有映射和行号。

### 4.1 两个来源族

PhotoCraft 里有两套相互独立、名字相近的调色实现，先选对族再查公式：

| 来源族 | PhotoCraft 位置 | 对应 PixCake 的什么 |
| --- | --- | --- |
| **A. Camera Raw 滤镜管线** | CPU：`crates/algo/src/camera_raw.rs`（`CameraRaw` 参数、`develop()` 九个阶段）、`crates/algo/src/camera_raw/per_pixel.rs`（白平衡/曝光增益、逐像素常量与表）、`crates/algo/src/camera_raw/local_tone.rs`（高光/阴影/清晰度/纹理的引导滤波）、`crates/algo/src/camera_raw/whites_blacks.rs`（白色/黑色的自适应曲线）；GPU：`crates/gpu/src/camera_raw.rs`、`crates/gpu/src/camera_raw.wgsl`；parity：`crates/gpu/tests/it/camera_raw.rs` 和 `camera_raw.rs` 内的单元测试；RAW 开发时的同类调整：`crates/raw/src/tone.rs` | 精修“基础/白平衡/HSL/曲线/色彩分级/颗粒”等显影滑杆，以及 `color.local`、`color.post` 中复用的同一组滑杆 |
| **B. Photoshop 调整图层与合成** | CPU：`crates/compose/src/adjust.rs`、`crates/compose/src/adjust/tone.rs`、`crates/compose/src/adjust/color.rs`、`crates/compose/src/psblend.rs`、`crates/compose/src/composite.rs`；GPU：`crates/gpu/src/compose.wgsl`、`crates/gpu/src/plan/lower.rs`；parity：`crates/gpu/tests/it/parity.rs`、`crates/compose/src/adjust/tone_tests.rs` | 亮度（按 2026-11-09 用户裁决采用 Photoshop 现代亮度）、色阶、可选颜色、3D LUT/色彩查找、混合模式、透明度和图层合成；其它 Photoshop 调整图层（黑白、色相/饱和度、色彩平衡等）只在 PixCake 语义确实相同时才能作为目标 |

PhotoCraft 的 Camera Raw GPU pass 只覆盖**逐像素阶段**（白平衡与曝光、不带纹理/清晰度时的对比度、白色/黑色、鲜艳度/饱和度、曲线、Color Mixer、色彩分级、暗角、颗粒）。去雾、高光/阴影、纹理、清晰度、锐化/降噪是**空间阶段**，PhotoCraft 只有 CPU 实现。PixCake 为这些阶段写 OpenGL 时没有 PhotoCraft GPU 可对照，必须以移植后的 C++ reference 为 oracle 做 parity，并在矩阵中单独写明。

### 4.2 PixCake 参数到来源族的对照

逐子参数的单位换算、公式和测试向量写在[公式登记](../evidence/2026-11-09-pixel-formula-registry.md)；这里只定来源族：

| PixCake 参数/分组 | 来源 | PhotoCraft 目标 |
| --- | --- | --- |
| `temperature`、`tint`、`exposure` | `photocraft-target` | A：`per_pixel::gains`（白平衡增益按亮度归一后乘 `2^exposure`），`camera_raw.rs` 阶段 1 在线性光中执行 |
| `contrast`、`highlights`、`shadows`、`whites`、`blacks`、`texture`、`clarity`、`dehaze` | `photocraft-target` | A：`camera_raw.rs` 阶段 2–3、`local_tone.rs`、`whites_blacks.rs` |
| `vibrance`、`saturation` | `photocraft-target` | A：`camera_raw.rs` 阶段 4（Presence） |
| `brightness` | `photocraft-target`（2026-11-09 用户裁决：按 Photoshop） | B：现代亮度/对比度的亮度部分，`compose/src/adjust/tone.rs:155` `modern_brightness`，以 `modern_lut(brightness, 0)` 逐通道作用于编码值；对比度仍走 A。Camera Raw 没有亮度滑杆，它插在 A 管线中的位置属于 `PixCake-specific` 顺序，见公式登记 |
| 曲线 | `photocraft-target` | A：阶段 5 参数曲线与 master/R/G/B 点曲线（`validate_curve`、`curve_lut`）；PixCake 曲线参数尚未注册 |
| HSL 八色带 `hsl_{hue,saturation,luminance}_*` | `photocraft-target` | A：阶段 6 Color Mixer；色带名与 PixCake 完全一致（red、orange、yellow、green、aqua、blue、purple、magenta） |
| 色彩分级 `grading_*` | `photocraft-target` | A：阶段 7 的阴影/中间调/高光/全局色轮与 Blending、Balance；不是 B 的 ColorBalance |
| 颗粒 `grain_amount/size/roughness` | `photocraft-target` | A：阶段 9 grain（按位置哈希，确定性） |
| 颗粒 `grain_shadows/midtones/highlights/color` | `PixCake-specific` 扩展 | Camera Raw 颗粒没有这些子参数 |
| 色阶 `levels_rgb_*` | `photocraft-target` | B：`Levels`（`adjust.rs` 与 `adjust/tone.rs` 的 `levels*`） |
| 可选颜色 `selective_*`、`selective_mode` | `photocraft-target` | B：`SelectiveColor`（九目标 × CMYK，相对/绝对） |
| 3D LUT/配置文件资源 | `photocraft-target`（采样） | B：`lut3d_sample`；资源内容、版本和许可证仍由 PixCake 管理 |
| 黑白配置文件与八色带黑白混合 `bw_mix_*` | `PixCake-specific` | Camera Raw 管线没有黑白处理；B 的 `BlackWhite` 只有六个权重，不能当作八色带等价 |
| 色彩编辑器偏移 | `no-equivalent` | PhotoCraft 没有 Point Color 等价实现 |
| AI 追色 `color.match`、AI 风格 `color.style` | `PixCake-specific` | 业务流程、点数和模型均由 PixCake 决定 |
| 混合模式、透明度、图层合成 | `photocraft-target` | B：`psblend.rs`、`composite.rs`、`compose.wgsl` |
| 输入色彩管理 | 参考 | PhotoCraft 的 transfer/CMS 只作参考；PixCake I/O 适配按依赖规范实现 |

PixCake 现有滑杆的范围与 Camera Raw 单位一致或可直接换算（曝光 ±5 EV，其余基础滑杆 −100…100，色轮色相 0…360、饱和度 0…100、明亮度 −100…100），移植时仍要逐项核对 `ParameterMode` 与黑盒证据。

### 4.3 PhotoCraft 其它可参考能力

下面的能力 PhotoCraft 有经典（非学习）实现，PixCake 对应功能实现前必须先判定能否作为 `photocraft-target`，不能直接默认为 `PixCake-specific`：

- 选择与蒙版：`crates/algo/src/segment`（Select Subject、天空 `sky.rs`、Quick Selection、GrabCut）、`crates/algo/src/matting`。PixCake 的人物/衣物语义蒙版沿用自己的分割方案；PhotoCraft 的经典算法可用于边缘细化或天空等无模型场景的参考，不替代 PixCake 的模型选择。
- 修复与重绘：`crates/algo/src/inpaint`、`content_aware`、`remove.rs`、`retouch.rs`（减淡/加深/海绵）。
- 变形：`crates/algo/src/liquify`、`warp.rs`；人脸：`crates/algo/src/faces.rs`（其网络权重是 BSD-3-Clause 的 libfacedetection，许可证与 PhotoCraft 本体不同）。
- 细节与效果：Camera Raw 阶段 8 的锐化/降噪、阶段 9 的暗角，以及 `crates/algo/src/sharpen*`、`denoise.rs`。
- 自动影调：`camera_raw.rs` 的 `auto_tone`。

PhotoCraft 的 `ml` 能力没有自动变成 PixCake 的生产依赖。人物、衣物、语义蒙版、AI 追色、AI 风格、点数账本和生成任务仍按 PixCake 的需求与进程边界实现；只有在 PhotoCraft 有明确等价算法时才复用该算法。

## 5. 移植要求

1. **先查 PhotoCraft，再写公式。** 新增或修改通用像素能力时，先按 4.1 选来源族，再在源码/测试中查找对应实现；找到了就登记为 `photocraft-target`，不得继续扩展 PixCake 自己的经验公式。
2. **按语义选族，不混用。** 一个滑杆只对应一个来源族；不能把 Camera Raw 的曝光和 Photoshop 调整图层的 gamma 拼成一条公式，也不能因为名字相同就把 Photoshop 调整图层当作 Camera Raw 滑杆的目标。
3. **保持阶段顺序。** Camera Raw 管线的九个阶段有固定顺序（`camera_raw.rs` 文件头）。PixCake 的 `RenderDescription` 操作顺序必须能表达这个顺序；若 PixCake 的产品顺序不同，在矩阵中写为 `PixCake-specific` 顺序并给出证据。
4. **CPU reference 是第一落点。** 先在 `gpu/render` 建立可测试的 C++ reference，再把同一语义翻译到 OpenGL shader。reference 与 shader 的执行顺序、transfer、量化、边界裁剪和 HDR 行为必须有对照测试。
5. **图像统计与预览/导出一致。** Camera Raw 的白色/黑色、阴影/高光锚点等阶段依赖整幅图像统计（例如 `luminance_points` 在最长边不超过 256 像素的副本上取百分位，使预览代理和全图一致），空间半径按 `pixel_scale` 缩放。PixCake 必须对整幅源图计算统计并在预览代理和全分辨率导出中复用；导出分块时不得逐块重新统计；预览代理必须传入与导出一致的半径换算。
6. **输入编码显式化。** Camera Raw 管线接收文档编码值（`encoding_gamma` 为 0 时按 sRGB 曲线解码，否则按幂律），各阶段在线性光与编码域之间切换。PixCake 保留[来源登记](2026-11-06-pixel-processing-provenance.md)中“executor 输入为线性”的契约，移植时在每个阶段按 PhotoCraft 所用的域显式做 transfer（parity 用 sRGB 曲线）；不能在“工作值是否已经线性”未闭环时隐式假设。
7. **RenderDescription 不变。** PhotoCraft 的 `CameraRaw`、`Adjustment`、`Layer`、wgpu plan 不能直接成为 PixCake 的公共协议。PixCake 仍由 `ParameterMode`、`RenderDescription`、`mask_ids`、`blend_mode`、revision 和诊断字段驱动。
8. **预览与导出共用执行路径。** `apps/renderd` 和 `apps/exportd` 继续消费同一套 PixCake executor/pass registry；PhotoCraft 的 CPU/GPU 分工不能制造第二套导出公式。
9. **OpenGL 是 PixCake 的 GPU 实现。** 业务渲染只使用 `pixcake::gl`，Qt OpenGL 只在 `gpu/backend/qt`，native OpenGL 只在 `gpu/backend/native`；`kernel` 不包含 Qt、OpenGL、wgpu 或渲染器私有类型。
10. **不把 PhotoCraft 架构倒灌进 PixCake。** 不因为参考项目使用 Rust、Cargo、wgpu、egui、TCP control server 或其 crate 层级，就改变 PixCake 的 C++17/CMake、Qt Widgets、四进程边界、静态能力注册、结果码、持久化、恢复和安全规则。
11. **能力照搬，业务规范自守。** 可以尽可能照搬 PhotoCraft 已实现的像素能力、公式、边界行为、测试向量和 GPU 计算顺序；项目目录、接口、命名、错误处理、资源生命周期、线程/进程和用户可见需求必须遵守 PixCake 规范。
12. **找不到等价实现就明确分流。** PhotoCraft 没有对应的能力时，写 `PixCake-specific` 或 `no-equivalent`，保留诊断和未完成状态；禁止用一个看起来相似的 PhotoCraft 公式冒充人物/语义/AI 产品能力。

## 6. 公式与来源记录

PhotoCraft 自己说明其公式是 Photoshop 行为的 documented approximations：`crates/compose/src/adjust.rs` 文件头写明通过 PSD corpus 调整；Camera Raw 的白色/黑色、高光/阴影、清晰度/纹理/去雾是在 Photoshop 25.4 上黑盒测量后拟合的（`crates/raw/src/tone.rs`、`crates/algo/src/camera_raw/whites_blacks.rs` 文件头），不是 Adobe 私有源码。因此 PixCake 文档必须写“PhotoCraft reference/port target”，不能写“Photoshop 官方内部公式”或“PixCake 官方公式”。

目前已经确认的典型来源：

- **白平衡与曝光（Camera Raw）**：`crates/algo/src/camera_raw/per_pixel.rs:13-20` 的 `gains()` 由 `white_balance_gains(temperature, tint)` 得到三通道增益、按亮度归一，再乘 `2^exposure`；`crates/algo/src/camera_raw.rs` 的 `balance()` 把文档值按 sRGB 解码到线性光、乘增益、非负裁剪后重新编码。这是 PixCake `temperature`/`tint`/`exposure` 的目标。Photoshop“曝光度”调整图层（`crates/compose/src/adjust.rs:138-157`，含 offset/gamma 和 gamma 2.2 transfer）不是 PixCake 曝光滑杆的目标。
- **影调（Camera Raw）**：对比度、高光/阴影、白色/黑色、清晰度/纹理、去雾见 `crates/algo/src/camera_raw.rs` 文件头的阶段 2–3 说明与 `local_tone.rs`、`whites_blacks.rs`。其中白色/黑色和阴影/高光锚点依赖图像统计，见第 5 节第 5 条。
- **色阶、可选颜色、3D LUT（调整图层）**：`crates/compose/src/adjust.rs` 的 `Levels`/`SelectiveColor`/`ColorLookup` 分派，`crates/compose/src/adjust/tone.rs:299-367`（levels）、`crates/compose/src/adjust/color.rs:271-323`（selective color）、`:127-185`（3D LUT 采样）。
- **GPU 与 lowering**：Camera Raw 逐像素阶段看 `crates/gpu/src/camera_raw.wgsl`；调整图层与合成看 `crates/gpu/src/compose.wgsl`、`crates/gpu/src/plan/lower.rs`。WGSL 只作为计算语义和参数绑定参考，生产实现翻译为 OpenGL/GLSL。

每条登记至少包含：PhotoCraft 来源族、相对路径、提交、PixCake 目标文件、参数单位换算、输入编码/位深、阶段顺序、图像统计依赖、CPU 向量、GPU readback、容差、未覆盖边界、来源和移植状态。当前 PixCake 公式可以保留在历史证据里，但不得在“行为来源”列继续写成 `project-decision`，除非该能力确实没有 PhotoCraft 等价实现。

## 7. 代码复制、独立重写和许可证

PhotoCraft 本体代码以 **`MIT OR Apache-2.0`** 双许可发布（`Cargo.toml` 与 `NOTICE` 写明“at your option”），即使用者二选一，不是同时满足两份。

- **PixCake 选用 MIT 条款。** 直接复制或翻译 PhotoCraft 代码、注释或 shader 时，在 PixCake 的分发物中保留 PhotoCraft 的版权声明（`Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors`）和 `LICENSE-MIT` 全文，并在 `third_party/photocraft.lock.cmake` 与对应证据中写明来源路径、提交和“MIT（选自 MIT OR Apache-2.0）”。移植文件头注明“Ported from PhotoCraft `<path>` @ `<commit>`, MIT”。若将来改选 Apache-2.0，需另行携带 NOTICE 并为修改过的文件添加修改说明。
- **独立用 C++/GLSL 重写**时不复制源代码表达，但仍记录 PhotoCraft 作为行为来源，并用 parity 证明语义一致。
- **不在 PhotoCraft 本体许可范围内的内容不得顺带使用**：`docs/brand/` 下的 ArtCraft 名称、字标和图标是商标；PhotoCraft 捆绑的字体、图标、词表和 libfacedetection 权重各有自己的许可证（见其 `NOTICE` 与 `ATTRIBUTION.md`）。PixCake 界面和分发物不使用 PhotoCraft/ArtCraft 品牌。
- 不复制 Photoshop 或其他闭源产品的源码、shader、profile、模型权重、素材和字符串。PhotoCraft 的 clean-room 规则仍然适用于它自己的参考仓库。
- `third_party/photocraft` 默认是参考副本，不是 PixCake 的运行时依赖，也不进入分发物。若未来要在构建或测试中执行 PhotoCraft 工具（例如生成 parity 向量），必须单独写 CMake/版本/许可证记录，生成的向量以数据文件提交，生产 `kernel` 或 OpenGL backend 不得链接 Rust/wgpu。

## 8. 验收门禁

PhotoCraft-backed 能力只有同时满足以下条件，才能在矩阵里写 `photocraft-ported`：

1. `RenderDescription` 的 PixCake 参数、单位换算和阶段顺序与 PhotoCraft 目标映射明确。
2. C++ CPU reference 已覆盖恒等值、正负边界、灰阶/颜色向量、位深和异常输入；依赖图像统计的阶段覆盖预览代理与全分辨率结果一致。
3. OpenGL 路径在真实 context 上完成 readback；无 context 时只能明确 skip/fallback。
4. CPU 与 OpenGL 的误差在测试声明的容差内（参考 PhotoCraft Camera Raw GPU 测试的 `1/4096`，8 位输出不超过 1 级），且至少有一个 PhotoCraft parity/corpus 向量或可复现的同等向量。PhotoCraft 只有 CPU 的空间阶段以 PixCake C++ reference 为 oracle。
5. 预览和导出使用同一执行路径，失败返回 PixCake 结果码和诊断，不生成占位像素。
6. 来源登记、许可证记录和移植文件头已经同步。

只有 C++ reference 存在时标为 `photocraft-reference-only`；只有旧 PixCake 公式时标为 `legacy-provisional`。任何一个状态都不能写成“PhotoCraft 已实现”或“官方像素算法已完成”。

## 9. 新会话开工清单

1. 先读本文，再读架构规范、RenderDescription 契约、状态矩阵和逐参数公式登记。
2. 检查 `git -C third_party/photocraft rev-parse HEAD` 与 `third_party/photocraft.lock.cmake` 一致；副本缺失时按第 10 节获取。把实际提交写进本次来源记录。
3. 判断任务的来源（`photocraft-target`、`PixCake-specific`、`no-equivalent`），PhotoCraft-backed 任务再判定来源族（A Camera Raw 或 B 调整图层/合成）；不要从当前 `render_executor.cpp` 的存在推断行为已定稿，也不要按名字选族。
4. PhotoCraft-backed 任务先定位 CPU/GPU/test 三类来源，再决定 C++/OpenGL 文件和测试，不改变 PixCake 模块边界。
5. 修改实现后同时更新状态矩阵、公式登记、来源/许可证记录和对应需求/计划；不要只改代码注释。
6. 结束前运行文档链接检查、`git diff --check` 和相关 CPU/GPU parity 测试；若 GPU/context 或真实 I/O 未闭环，保留明确的未完成状态。

## 10. 参考副本的锁定与获取

- 锁定记录：`third_party/photocraft.lock.cmake`，写明上游 URL、提交、许可证选择和用途（参考，不参与构建）。
- 副本本身（约 120 MB，含自己的 `.git`）不提交进 PixCake 仓库，已加入 `.gitignore`。新机器按锁定文件获取：

  ~~~sh
  git clone --filter=blob:none https://github.com/storytold/photocraft.git third_party/photocraft
  git -C third_party/photocraft checkout <PIXCAKE_PHOTOCRAFT_COMMIT>
  ~~~

- 更新提交时：改锁定文件 → 重新核对第 4 节和公式登记中的路径与行号 → 在矩阵中记录更新日期。

这份文档是“能力来源与架构边界”的总裁决。若旧计划、旧证据或代码注释与本文冲突，保留历史事实但按本文更新当前状态和下一步；不能选择性引用旧文档来恢复 PixCake 暂定公式的权威地位。
