# 精修衣物名称读取记录

日期：2026-11-05

来源应用：`/Applications/pixcake-test.app`
版本：9.3.0
Jarvis 页面图：`Pixcake_mac` 9.3.0，节点 `refine_clothing_slider`

## 现场页签

辅助功能树里的衣物页签文字是「衣物」，描述是「衣物美化」。点击后该页签值为选中。面板正文没有读到衣物滑杆文字，因此可见短名只做页面图全名的共同前缀切分，不另起名字。

共同前缀是「衣物美化-衣服调整-」。切掉之后的文字作为界面可见名，无障碍名保留页面图全名。

## 十三项

顺序与页面图一致。

| 可见名 | 无障碍名 |
| --- | --- |
| 上衣祛褶皱 | 衣物美化-衣服调整-上衣祛褶皱 |
| 上衣祛褶皱-祛粗褶皱 | 衣物美化-衣服调整-上衣祛褶皱-祛粗褶皱 |
| 上衣祛褶皱-祛细褶皱 | 衣物美化-衣服调整-上衣祛褶皱-祛细褶皱 |
| 下装祛褶皱 | 衣物美化-衣服调整-下装祛褶皱 |
| 下装祛褶皱-祛粗褶皱 | 衣物美化-衣服调整-下装祛褶皱-祛粗褶皱 |
| 下装祛褶皱-祛细褶皱 | 衣物美化-衣服调整-下装祛褶皱-祛细褶皱 |
| 衣服祛汗渍 | 衣物美化-衣服调整-衣服祛汗渍 |
| 衣服祛瑕疵 | 衣物美化-衣服调整-衣服祛瑕疵 |
| 衣服祛褶皱 | 衣物美化-衣服调整-衣服祛褶皱 |
| 衣服祛褶皱-祛粗褶皱 | 衣物美化-衣服调整-衣服祛褶皱-祛粗褶皱 |
| 衣服祛褶皱-祛细褶皱 | 衣物美化-衣服调整-衣服祛褶皱-祛细褶皱 |
| 衣服边缘平整 | 衣物美化-衣服调整-衣服边缘平整 |
| 道具保护 | 衣物美化-衣服调整-道具保护 |

没有读到范围、步进或默认值。不写入界面。设计文档里另有的衣服边缘修复和涂抹笔不在这 13 项里，不加入。

## 验收命令（2026-11-05）

工作目录是仓库根目录，分支仍是 `codex/desktop-official-layout`。CTest 没有全部通过。已有二进制的 29 项测试通过。`test_render_description` 没有运行。未提交的 `tests/CMakeLists.txt` 把它登记到 `kernel/render/test_render_description.cpp`。该源文件现在在工作区里，是未跟踪文件，并不是缺失；缺失的是可执行文件 `build-desktop/tests/test_render_description`。同一次 CTest 里还有 4 项同样因可执行文件不存在而没有运行。这里不修改 `tests/CMakeLists.txt`。不把这次 CTest 写成全部通过。

### ctest --test-dir build-desktop --output-on-failure

退出码 8。输出结尾是：

```
85% tests passed, 5 tests failed out of 34

The following tests FAILED:
	 13 - test_color_adjustment_panel (Not Run)
	 19 - test_render_description (Not Run)
	 20 - test_preview_coordinator (Not Run)
	 31 - test_gpu_facade (Not Run)
	 32 - test_gpu_backend_qt (Not Run)
Errors while running CTest
```

已有二进制的 29 项测试通过，包括 `test_shell_app_smoke_refine`、`arch_dependencies` 和 `capability_ownership`。

`test_render_description` 没有运行：尚未提交的 `tests/CMakeLists.txt` 把它登记到 `kernel/render/test_render_description.cpp`。源文件 `tests/kernel/render/test_render_description.cpp` 存在且未跟踪，可执行文件 `/Users/lifangzai/PixCake-MVP/build-desktop/tests/test_render_description` 不存在，CTest 报 Unable to find executable。不把这项写成通过。

另外四项也是 Not Run，原因同样是可执行文件不存在。对应源文件在工作区且未跟踪，CMake 登记尚未提交：

- `test_color_adjustment_panel`：`tests/ui/test_color_adjustment_panel.cpp`
- `test_preview_coordinator`：`tests/kernel/preview/test_preview_coordinator.cpp`
- `test_gpu_facade`：`tests/gpu/test_gpu_facade.cpp`
- `test_gpu_backend_qt`：`tests/gpu/test_gpu_backend_qt.cpp`

### cmake --build build-desktop --target architecture_gate

退出码 0。该命令先重新配置并生成了 `build-desktop`（Configuring done、Generating done），然后输出 `-- arch dependencies ok`，接着 `Built target architecture_gate`。重新配置之后，上面五项测试的可执行文件仍然不存在。

### git diff --check

对已跟踪改动无输出，退出码 0。把本记录加入暂存后，`git diff --cached --check` 报出第 5–6 行行尾空格。这两行的行尾空格已删除，复查通过。

### 壳层对照

13 个可见名和对应的完整无障碍名已经放进对象名 `refine-clothing-labels`。可见名是页面图全名去掉共同前缀后的短名，无障碍名是页面图全名。没有加入数值。
