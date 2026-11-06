# Kernel 预览与绘制管线闭环证据

日期：2026-11-05

本阶段在并行 OpenGL 后端、`gpu/render` 和 UI 之外，完成了内核侧闭环：

- `kernel/capability` 为阶段一管线声明依赖边，校验未知依赖、重复步骤和环路，并提供稳定能力顺序。
- `kernel/preview` 的 `PreviewCoordinator` 校验目标、编辑实例、修订号、参数模式和最长边，只保留最新预览请求。
- `kernel/render` 编译参数操作、图层、蒙版和选择区为无后端对象的纯描述；图层混合模式和越界状态有诊断。
- `kernel/ipc` 冻结预览请求、取消消息和 RGBA 帧头，编解码拒绝截断、过长、未知版本和非法尺寸数据。

验证命令：

```text
cmake -S . -B /private/tmp/pixcake-kernel-preview-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-preview-20261105 --parallel 8
ctest --test-dir /private/tmp/pixcake-kernel-preview-20261105 --output-on-failure
git diff --check
```

结果：**35/35 CTest 通过**，包括新增的 `test_preview_coordinator`、`test_preview_protocol`、图层/蒙版渲染描述测试、GPU 并行线测试，以及架构依赖和能力归属门禁。

本证据不宣称完成 OpenGL 着色、GPU 帧缓冲、共享内存实现、UI 控件、真实图片解码或远端 AI；这些仍由并行模块或后续独立计划负责。
