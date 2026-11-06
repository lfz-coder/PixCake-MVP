# GPU 后端审查证据

日期：2026-11-05
范围：`gpu/backend/qt`、`gpu/backend/native`、`gpu/facade`、Glad loader 和对应 CMake/合同测试。不包含 `gpu/render`，也不替代其他 worktree 的渲染执行器验收。

## 实现

- `pixcake::gl` 公共 facade 只暴露标准库类型和稳定资源 API；Qt 类型只在 `gpu/backend/qt` 的实现翻译单元出现。
- Qt 后端实现 context、shader、program、uniform、buffer、vertex array、texture、framebuffer 和 fence。
- native 后端在 macOS 使用 CGL Core profile context，并通过仓库内 Glad Core 3.3 loader 调用原生 OpenGL；同样的资源 API 不依赖 Qt。
- native 平台实现留在 `gpu/backend/native`；与 Qt 后端一样实现 facade 的 `detail::*Impl` 接口，由 `pixcake::gl::native::create_context()` 在运行时注入，不改变 facade 名称。
- 当前 native shader 路径覆盖 Vertex/Fragment；Compute Shader 需要 OpenGL 4.3，本阶段 Core 3.3 合同不把它作为必需能力。

## 验证

```text
cmake -S . -B build-gpu-native
cmake --build build-gpu-native --target test_gpu_backend_native architecture_gate -j2
ctest --test-dir build-gpu-native -R '^(test_gpu_backend_C57_C60|arch_dependencies|capability_ownership)$' --output-on-failure
```

结果：native backend 构建、macOS Core OpenGL runtime smoke、`arch_dependencies` 和 `capability_ownership` 均通过。Qt 配置使用同一合同测试和架构门禁通过；无可用 offscreen provider 时只跳过 Qt runtime smoke，并保留 API/error-path 断言。

## 审查边界

Q-27 已从“禁止 native 函数体”改为“禁止把 native 实现放入 facade/`gpu/render` 或引入 Qt/业务参数”。C-57、C-60 的公共 API 和 Qt 链接边界保持不变。跨进程日志、预览帧、导出统一管线和 `gpu/render` 执行器不属于本 worktree 的验收范围。

## 合并到主线（2026-11-05）

合并 `codex/gpu-backends` 时以 render-execution 的 facade/executor API 为准：原先的编译期别名（`PIXCAKE_GL_BACKEND` + `gl_types.hpp`）被移除，native 后端改写为实现主线 facade 的 `detail::*Impl` 接口，额外提供 `create_buffer`/`create_vertex_array`/`create_fence`。合同测试改为 `tests/gpu/test_gpu_backend_native.cpp`（CTest 名仍为 `test_gpu_backend_C57_C60`），并新增一条断言：同一 `RenderExecutor` 在 native context 上的输出与 reference 路径一致。本机全量 CTest 51/51 通过，native runtime smoke 实际执行（非跳过）。
