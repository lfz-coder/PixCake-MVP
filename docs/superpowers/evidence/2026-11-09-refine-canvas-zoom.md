# 精修画布显示比例与缩放（2026-11-09）

## 计划对应

`docs/superpowers/plans/2026-11-07-remaining-work.md` 的补充项把设计文档 §3.7、§23.1 中原来未接的画布显示比例、抓手、滚轮和快捷键列为实现项。本次补齐了壳层显示状态；按照内核架构约束，缩放值不进入精修参数文档，也不触发一次编辑提交。

## 实现

- `apps/shell/refine_page.cpp` 将“显示比例调整”渲染为深色 Mac 编辑器风格的 `refine-zoom-ratio` 下拉触发器；弹窗圆角 8px、背景 `#3C3C3E`、SF Pro 字体、白色文字、44px 行高、`#505054` 全宽分割线和 `#515154` hover 行。菜单分组为“合适（⌘0）”、`1:1`、`5%`/`50%`、`100%`/`200%`/`300%`/`400%`/`800%`/`1200%`，没有勾选图标。选择项与 `PixCakeImageView` 的缩放状态同步。
- `apps/shell/pixcake_widgets.hpp` 的 `PixCakeImageView` 保存显式缩放倍数、居中偏移和抓手状态。显式 100% 以一张图像像素对应一个逻辑画布单位；“合适”保留原来的等比适配。抓手进入精修时默认开启，拖动图像；普通滚轮按指针位置缩放，平移不再由滚轮承担。
- 点击已经启用的抓手工具不会改变状态。画笔/局部蒙版拖动、裁剪和创建参考线会自动关闭抓手，避免同一鼠标手势冲突；`H` 重新启用抓手，`Cmd/Ctrl+=`、`Cmd/Ctrl+-`、`Cmd/Ctrl+0` 分别放大、缩小和恢复默认尺寸。`imageRect()` 是取色、蒙版拖拽和渐变百分比的唯一反算来源，因此缩放后仍落在同一图像坐标。
- 显示层按倍率选择采样：1:1 直接绘制原图并关闭平滑过滤；缩小时按物理显示尺寸建立最多每级缩小约 2 倍的 `QImage::scaled(..., Qt::SmoothTransformation)` 金字塔缓存；放大时不生成超大缓存位图，直接绘制源图并开启 `QPainter::SmoothPixmapTransform`。这是 Qt 显示层的自适应平滑策略，不是新增的内核重采样算法：预览解码仍由 ImageIO 在解码时缩小，语义蒙版使用 XNNPACK 双线性，导出排版遵循缩小时面积平均、放大时双线性的既有实现。

## 验证

精修冒烟流程检查了：

1. 下拉触发器和“合适⌘0 / 1:1 / 5%、50% / 100%–1200%”菜单项存在，四个快捷键对象已注册；
2. 选择 100% 后 `imageRect()` 的宽高与预览帧像素尺寸一致；
3. 选择“合适”后恢复适配显示；
4. 默认抓手开启，点击抓手工具保持开启，普通滚轮按指针位置改变缩放；参考线菜单等冲突工具会关闭抓手；
5. 画布采样模式在 100%、缩小和放大时分别进入 Direct、DownsamplePyramid、SmoothUpscale；
6. 原有曝光、黑白、颗粒、裁剪旋转、局部渐变拖拽仍通过同一套画布映射运行。

执行结果：

```text
cmake --build build-desktop --target pixcake_app_shell -j2   PASS
ctest --test-dir build-desktop -R 'test_shell_app_smoke_refine|test_renderd$|test_renderd_image_io|test_geometry' --output-on-failure   4/4 PASS
ctest --test-dir build-desktop -R 'test_shell_app_open_photo|test_shell_app_import_flow' --output-on-failure   2/2 PASS
```

最后一组真实图片测试需要允许 renderd 建立本机 Unix socket；在受限沙箱中会得到 `Operation not permitted`，放开该本机进程权限后两项均通过。边界清单中的 B-43（缩放比为 0 或负数）现在由 `PixCakeImageView::setZoom` 拒绝并保留上一次有效显示；用户选择下拉中的“合适”则走独立的 `setFitZoom`。B-51（滚轮和窗口缩放不能等待主线程）由纯事件处理、无阻塞调用的画布路径覆盖，真实图片冒烟流程也会重建预览和交互。
