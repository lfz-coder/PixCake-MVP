# 官方桌面视觉壳层验收

日期：2026-11-05  
计划：官方页面图级视觉复刻计划（继续执行）

## 结果

本轮完成共享视觉层和五页可证实壳层对齐，未宣称完成真实业务或完整像素复刻。`apps/shell/pixcake_widgets.hpp` 提供共享按钮、面板、空状态和分割线；五页继续复用 `PixCakeTheme` 的颜色、边框、面板和控件状态 token。

## 自动化

- 五页 `--smoke`：通过（login/home/library/refine/creative）。
- `ctest --test-dir build-desktop --output-on-failure`：通过。
- `cmake --build build-desktop --target architecture_gate`：通过。
- `check_dependencies.cmake`：通过。
- `check_capability_ownership.cmake`：通过。
- `git diff --check`：通过。

## Jarvis AX

Jarvis executor `0.1.586`，Accessibility 与 Screen Recording 可用；本地窗口标题为“像素蛋糕”，窗口内容约 1386×832。已验证创意页可读到：图库/精修/创意切换、AI工具/历史/批量/帮助/导出、12 个手动工具、画布空状态、AI工具箱/侧边栏/图层面板和底部缩略入口。页面按钮和业务动作保持禁用；未产生文件、网络、登录、GPU、AI 或导出副作用。

登录页 Jarvis 结构与用户截图要求一致：账号输入框、验证码输入框（maxLength=6）、获取验证码、禁用登录、协议行、帮助入口；人物资源已从构建目录正确放入 bundle `Contents/Resources/login-hero.png` 并能在窗口中显示。

## 未完成 / pending

- 官方远程原图仍无法解析；当前使用用户提供的六张本地官方截图作为视觉基准。
- 独立 Logo、微信图标、帮助图标、官方字体和无标注原图未取得。
- 官方截图中的示例项目、照片、缩略图、会员数字、AI 结果和图层不复制为业务数据。
- 真实登录、导入、照片解码、渲染、AI、图层、导出、云空间、工作流、选片交付和 F01–F13 继续延期。
