# 官方桌面视觉复刻基线

日期：2026-11-05  
计划：`docs/superpowers/plans/2026-11-04-official-pixel-visual-replica.md` 继续执行  
分支：`codex/desktop-official-layout`  
阶段：0（证据和基线冻结）

## 当前工作状态

```text
branch: codex/desktop-official-layout
head: 94e510f docs(desktop): plan next official desktop implementation
worktree: normal checkout; unrelated kernel changes preserved
qt: qtpaths 未在当前 shell 返回版本；构建配置使用 /opt/homebrew/opt/qtbase
cmake: 4.1.2
jarvis: executor 0.1.586; AX available; screen recording available
official-assets: local user-provided screenshots available; remote originals blocked
current-phase: 0 -> 1
pending: exact original typography, independent logo/icon files, original unannotated screenshots,
         runtime photo/project data, true business behavior
```

## 五页固定比较基准

| 页面 | 官方基准 | 原始尺寸 | 本地运行参数 | 运行时截图命名 | 说明 |
|---|---|---:|---|---|---|
| 登录 | `assets/login.png` | 2048×1353 | `--page login` | `local-login.png` | 黄色编号线属于说明层，不绘制 |
| 主页 | `assets/home.png` | 2160×1486 | `--page home` | `local-home.png` | 示例项目照片和数量不作为运行时数据 |
| 精修 | `assets/refine.png` | 2160×1486 | `--page refine` | `local-refine.png` | 示例照片和参数不作为运行时数据 |
| 图库 | `assets/library.png` | 2160×1486 | `--page library` | `local-library.png` | 网格照片、编号、标签保持空状态 |
| 创意 | `assets/creative.png` | 2160×1486 | `--page creative` | `local-creative.png` | 示例人物图和图层保持空状态 |
| 图库预设补充 | `assets/preset-library.png` | 2048×1244 | 仅对照 | `local-library-presets.png` | 只用于确认预设栏层级，不加载业务预设 |

本地产品窗口统一以应用启动后的内容区域为比较对象；系统窗口边框、截图白色外边距和官方黄色编号说明线不属于运行时 UI。若窗口管理器或屏幕缩放导致物理尺寸变化，比较记录以内容区域比例和相对布局为准。

## 证据规则

1. 官方截图决定大区域比例、背景层级、控件密度和可见文字。
2. 官方手册正文决定功能名称和范围；截图中的项目名、照片、会员数字、缩略图和 AI 结果不复制。
3. Jarvis 只验证窗口、页面路径、AX 文本、禁用状态和空状态，不替代像素比较。
4. 无法从本地截图确认的颜色、字体、图标、原始裁切和默认值标记为 `pending`，不写成最终设计常量。
5. 当前 `apps/shell/login-hero.png` 是从用户提供登录截图裁出的本地展示资源；其余页面不添加未经确认的业务图片。

## 远程资源状态

手册页面中解析到的官方图片 URL 在本机 shell 中无法解析/超时。当前六张用户提供附件已经冻结到 `docs/superpowers/evidence/assets/`，足以执行本轮壳层视觉对齐；未取得的独立 Logo、微信图标、帮助图标和无标注原图继续列为 `pending`。
