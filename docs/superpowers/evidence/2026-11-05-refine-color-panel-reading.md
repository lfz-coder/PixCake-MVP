# 精修调色面板读取记录

日期：2026-11-05

来源应用：`/Applications/pixcake-test.app`
进程名：像素蛋糕-test
版本：9.3.0（811）
Jarvis 页面图：`Pixcake_mac` 9.3.0
执行机：0.1.586，辅助功能和屏幕录制可用

## 读取方式

Jarvis 知识库这次没有连上，没有使用工作流。页面名来自执行机上的页面图，控件名来自该应用当前精修窗口的辅助功能树。`macdom` 没有监听，不能从应用自己的界面树读取滑杆范围。

`mac_go_to_page` 目标「白平衡-色温」停在精修页，返回 `entry_unavailable`：页面图里的 `refine->refine_color` 入口在当时界面上找不到。随后直接读辅助功能树，调色页签处于选中状态。没有改滑杆，没有点选其他模块。

官网 `https://www.pixcakeai.com/guide/32445` 这次抓取结果是样式表，正文里没有「白平衡」「色温」「色彩调节」。因此这批名称以 9.3.0 现场读取为准，不把设计文档里的范围写成已核对。

## 已核对的模块页签

辅助功能描述写在括号里。没有读到描述的，不补写。

| 可见名 | 描述 | 当时是否选中 |
| --- | --- | --- |
| 调色 | 色彩调节 | 是 |
| 局部 | 局部调色 | 否 |
| 人像 | 人像美化 | 否 |
| 图像 | 图像美化 | 否 |
| 衣物 | 衣物美化 | 否 |
| 后调色 | 后期调色 | 否 |
| 裁剪 | 裁剪、旋转 | 否 |
| 历史 | 历史记录 | 否 |
| 联机 | 联机拍摄 | 否 |

AI色彩页签可见，这次没有读到描述。页面图里有「宠物」，这次辅助功能树没有列出宠物页签，所以不把宠物写成这次新核对的结果。壳层里已有的「宠物」按钮保持不动。

## 调色面板里已核对的名称

按现场纵坐标。这些是名称，不是数值。

开启 AI 色彩风格、配置文件、色彩调节、预设、滤镜、推荐、我的、AI 校准、程度、白平衡、AI校准白平衡、白平衡选择器、色温、色调、影调、AI校准曝光、曝光、对比度、亮度、高光、阴影、白色、黑色、纹理、清晰度、祛雾、鲜艳度、饱和度、色阶、曲线、HSL、色彩编辑器、颜色分级、可选颜色、细节、颗粒、镜头调整、透视矫正、校准、创意色彩。

页面图 `refine_color_slider` 还列出了这些名称下面的更长参数名，例如「白平衡-色温」「影调-曝光」。那份清单只证明名称存在于页面图，没有给出最小值、最大值、步进或默认值。

## 明确不采用的数字

现场滑杆的辅助功能文字包含 70、102、-1、4650 和多个 0。这是当时打开的照片读数，不是范围，也不是默认值。

内核 `ParameterMode` 里的色温、曝光等范围没有被这次读取证实。曝光附近的滑杆文字是 4650，和内核里的 -5 到 5 对不上。计划不把内核范围抄进界面。

AI校准白平衡、AI校准曝光、推荐在当时是勾选状态。这是当前照片或当前页的状态，不写成产品默认。

相机信息行读到的是「ISO -」「- mm」「F/-」「-秒」，没有真实参数。测试包里还有一个文字为 test 的控件。这两类都不进入界面。

## 验收命令（2026-11-05）

工作目录是仓库根目录，分支仍是 `codex/desktop-official-layout`。CTest 没有全部通过。已有二进制的 29 项测试通过。`test_render_description` 没有运行，因为一份与本计划无关、尚未提交的 CMake 登记指向缺失的源文件。这不记为本任务的缺陷，这里不修改 `tests/CMakeLists.txt`、kernel 或主题文件。

### ctest --test-dir build-desktop --output-on-failure

退出码 8。输出结尾是：

```
97% tests passed, 1 tests failed out of 30

The following tests FAILED:
	 18 - test_render_description (Not Run)
Errors while running CTest
```

已有二进制的 29 项测试通过，包括 `test_shell_app_smoke_refine`。`test_render_description` 没有运行：尚未提交的 `tests/CMakeLists.txt` 把它登记到 `kernel/render/test_render_description.cpp`，该源文件不存在，因此也没有可执行文件 `/Users/lifangzai/PixCake-MVP/build-desktop/tests/test_render_description`。不把这次 CTest 写成全部通过。

### cmake --build build-desktop --target architecture_gate

退出码 0。结果是 `-- arch dependencies ok`，然后 `Built target architecture_gate`。

### git diff --check

对已跟踪改动无输出，退出码 0。把本记录加入暂存后，`git diff --cached --check` 报出第 5–8 行行尾空格。这四行的行尾空格已删除，复查通过。

### 壳层对照

精修侧栏有九个模块按钮带无障碍描述：调色（色彩调节）、局部（局部调色）、人像（人像美化）、图像（图像美化）、衣物（衣物美化）、后调色（后期调色）、裁剪（裁剪、旋转）、历史（历史记录）、联机（联机拍摄）。

「色彩调节」组包含已核对的 40 个名称，没有滑杆数值。滑杆范围、默认值和当前照片读数仍然没有。
