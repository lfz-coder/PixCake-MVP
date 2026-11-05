# GPU 后端审查证据

日期：2026-11-05
范围：`gpu/backend/qt`、`gpu/backend/native`、`gpu/facade`、Glad loader 和对应 CMake/合同测试。不包含 `gpu/render`，也不替代其他 worktree 的渲染执行器验收。

## 实现

- `pixcake::gl` 公共 facade 只暴露标准库类型和稳定资源 API；Qt 类型只在 `gpu/backend/qt` 的实现翻译单元出现。
- Qt 后端实现 context、shader、program、uniform、buffer、vertex array、texture、framebuffer 和 fence。
- native 后端在 macOS 使用 CGL Core profile context，并通过仓库内 Glad Core 3.3 loader 调用原生 OpenGL；同样的资源 API 不依赖 Qt。
- native 平台实现留在 `gpu/backend/native`；业务调用点通过 `PIXCAKE_GL_BACKEND=qt|native` 选择，不改变 facade 名称。
- 当前 native shader 路径覆盖 Vertex/Fragment；Compute Shader 需要 OpenGL 4.3，本阶段 Core 3.3 合同不把它作为必需能力。

## 验证

```text
cmake -S . -B build-gpu-native -DPIXCAKE_GL_BACKEND=native
cmake --build build-gpu-native --target test_gpu_backend architecture_gate -j2
ctest --test-dir build-gpu-native -R '^(test_gpu_backend|arch_dependencies|capability_ownership)$' --output-on-failure
```

结果：native backend 构建、macOS Core OpenGL runtime smoke、`arch_dependencies` 和 `capability_ownership` 均通过。Qt 配置使用同一合同测试和架构门禁通过；无可用 offscreen provider 时只跳过 Qt runtime smoke，并保留 API/error-path 断言。

## 审查边界

Q-27 已从“禁止 native 函数体”改为“禁止把 native 实现放入 facade/`gpu/render` 或引入 Qt/业务参数”。C-57、C-60 的公共 API 和 Qt 链接边界保持不变。跨进程日志、预览帧、导出统一管线和 `gpu/render` 执行器不属于本 worktree 的验收范围。
