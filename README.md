# PixCake-MVP

## 开工前必须读的实现来源约定

PixCake 的通用像素能力以 [PhotoCraft 能力来源与 PixCake 移植规范](docs/superpowers/specs/2026-11-09-photocraft-reference-and-porting-policy.md) 为准：PhotoCraft 已实现的能力、公式、边界行为和测试优先照搬并移植到 PixCake 的 C++17/OpenGL 执行路径；PhotoCraft 的 Rust、wgpu、egui 和 crate 架构不进入 PixCake。

PixCake 自己的用户需求、Qt Widgets 壳层、`kernel`/`renderd`/`exportd`/`generated` 进程边界、`RenderDescription`、`pixcake::gl`、OpenGL backend、持久化、结果码和业务规范继续以 [第一阶段需求](docs/superpowers/specs/2026-11-02-pixcake-desktop-phase1-design.md) 与 [架构设计规范](docs/superpowers/specs/2026-11-02-pixcake-kernel-architecture-design.md) 为准。PhotoCraft 没有等价实现的 AI、语义蒙版、项目和商业能力标记为 PixCake-specific，不伪造 PhotoCraft 来源。

参考副本放在本地 `third_party/photocraft`（不入库），上游地址与锁定提交见 [`third_party/photocraft.lock.cmake`](third_party/photocraft.lock.cmake)；来源族划分、公式映射、parity 门禁和许可证选择（MIT，选自 `MIT OR Apache-2.0`）见上述移植规范。像素处理当前状态以 [执行现状矩阵](docs/superpowers/evidence/2026-11-09-pixel-processing-status-matrix.md) 和 [逐参数公式登记](docs/superpowers/evidence/2026-11-09-pixel-formula-registry.md) 为准。
