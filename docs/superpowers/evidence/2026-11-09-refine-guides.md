# 精修参考线接通证据（2026-11-09）

## 结论

参考线此前只有 `refine-tools/guides` 的文案、图标和需求记录，`workspaceTool` 是静态 `QFrame`，没有状态、点击回调或画布绘制。现已接通到 `PageActions`/`ShellController`，状态作为精修工作区叠加层保存于当前会话，不进入精修参数文档。

## 行为

- 点击工具栏参考线按钮打开深色圆角菜单：创建水平参考线、创建垂直参考线、隐藏/显示参考线；菜单包含分割线、hover 高亮、禁用态和对应快捷键。
- 参考线按钮菜单中的创建项分别创建水平或垂直参考线；快捷键仍可直接执行对应操作。
- 隐藏参考线后，两个创建项禁用，创建快捷键也不会新增参考线；显示参考线后恢复可用。
- `Ctrl/Cmd+R` 创建水平参考线，`Shift+R` 创建垂直参考线，`Alt/Option+R` 隐藏或显示参考线。
- 新建参考线默认位于画布中心，允许多条同向参考线完全重叠。
- 参考线位置按整个画布控件的宽高百分比计算，贯穿整个画布，不随图像矩形、缩放或平移改变；左键拖动改变位置。
- 点击参考线会选中并高亮；选中后按 Delete/Backspace 删除当前参考线。
- 在线上右键可删除当前线或全部参考线。
- 切换照片或关闭预览时清空临时参考线；隐藏只改变显示，不丢失线的位置。

## 代码边界

- `apps/shell/guide_line.hpp` 定义临时叠加层数据。
- `PageActions` 暴露创建、显示、移动、删除和 watcher 接口。
- `ShellController` 持有状态并通知画布，不触碰参数文档、渲染命令或磁盘。
- `PixCakeImageView` 负责绘制和鼠标命中/拖动/右键菜单；`refine_page.cpp` 只连接回调。
- 文案和快捷键来自 `ui/shell/src/page_copy.cpp`，没有在页面源码写中文或按键字面量。

## 验证

通过：

```text
cmake --build build-desktop -j2 --target pixcake_app_shell test_shell_sources
ctest --test-dir build-desktop -R 'test_shell_sources|test_shell_display' --output-on-failure
QT_QPA_PLATFORM=offscreen build-desktop/apps/shell/pixcake_app_shell.app/Contents/MacOS/pixcake_app_shell --page refine --smoke
```

当前沙箱内直接运行 `test_shell_app_open_photo` 会因 renderd 监听 socket 返回 `Operation not permitted`，因此真实预览帧冒烟未能在本次环境完成；代码级冒烟覆盖了菜单三项、重叠、画布坐标、移动、选中和 Delete 删除，精修空页面 smoke、`test_shell_sources`、`test_shell_display` 已通过。
