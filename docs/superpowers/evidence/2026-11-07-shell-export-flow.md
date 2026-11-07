# 壳层导出：导出弹窗、壳层↔exportd 进程通信、导出列表（2026-11-07）

需求第 19 节；弹窗字样、布局与默认值来自 `2026-11-06-export-dialog-observation.md`（像素蛋糕-test 9.3.0 实测）。范围是用户 2026-11-06 同意的：做快速导出 / 自定设置两页、导出至 + 路径、格式 原始 / JPG / TIFF8 / TIFF16、品质 0–14、转换为 sRGB、进程通信、导出列表；预设、水印、排版印刷、文件大小限制、上传选片交付、保留叠加层在界面上置灰并说明原因。

## 链路

```
壳层（Qt）                      services/export_session              exportd 进程
导出弹窗 ──PageActions──▶ ShellController ──▶ ExportSession.submit
                                              │ BusinessCommands::enqueue_export（登录才可）
                                              │ 写 <数据目录>/exports/export.db（导出事件日志）
                                              └ ExportClient ──Unix 套接字──▶ ExportServer
                                                 ExportStartMessage（任务号 + 原图路径）   │ 读同一份日志找任务
                                                                                         │ 文档快照 → render::compile
                                                                                         │ ExportService：解码 → GPU/参考 → 编码
                                                                                         │ ExportQueue：临时文件 → 校验 → 发布
导出列表 ◀── rows()（读日志）◀────────── ExportProgressMessage（状态、失败原因）◀──┘
```

- 进程消息（`kernel/ipc`）：新增 `ExportStartMessage`（种类 12：任务号、原图路径）；`ExportProgressMessage` 加可选字段 6“原因”。任务本身（参数快照、目标路径、设置）只在导出日志里，消息不重复携带。
- 原图路径由壳层在交出任务时经书签解析（原图可能被移动过）；解析不到就把任务记为失败（原因“原图不存在或无法访问”），不交给 exportd。
- exportd：`--socket <路径> --data-dir <数据目录>`，一次只做一个任务、按到达顺序；壳层断开后未开始的任务留在日志里，下次打开时 `resume()` 再交。`TaskCancelMessage` 只取消还没开始的任务。
- 失败原因写进 `fail` 事件的 `phase` 列（`ExportQueue::fail(id, code, reason)`、`failure_text(id)`），重启后导出列表仍能显示；重试（`requeue`）后清空。
- 导出列表每 0.5 秒读一次日志，另外每条进度消息到达时立即刷新；失败的可“重试”，排队的可“取消任务”，成功的可“打开所在文件夹”。

## 恢复只能由跑导出的进程做（本次发现并修复的缺陷）

`ExportBook::open` 原来每次打开都做崩溃恢复。导出列表刷新时壳层打开日志，正好撞上 exportd 在写临时文件：恢复把它当成崩溃，删了临时文件、把任务记为失败，exportd 随后的提交被拒（日志里是“export queue rejected the failure transition”）。

修复：`ExportBook::open(..., Recovery::Skip)`。壳层只在 `ExportSession::open` 时（exportd 还没启动）恢复一次，之后所有读取都跳过恢复；exportd 在任务之间打开日志时照常恢复。回归测试 `test_reader_without_recovery_leaves_running_task_alone`；把 `Skip` 改回 `Recover`，三条断言失败。

顺带修了 4 个恢复测试的 `main` 永远返回 0 的问题（`test_export_recovery`、`test_ledger_replay`、`test_layer_recovery`、`test_history_restart`）。之前其中断言失败只打印，ctest 仍判通过；改后 4 个都真实通过。

## 本项目暂定、官方未核实的选择

| 项 | 本项目做法 | 实测到的 |
| --- | --- | --- |
| 默认导出文件夹的位置 | 原图文件夹旁边的“项目名_导出” | 只看到名字是“项目名_导出” |
| 新建项目文件夹存储（默认勾） | 在所选文件夹里再建“项目名”子文件夹 | 只看到勾选框和默认值 |
| 同名文件 | 不覆盖，依次命名 `名(1).jpg`、`名(2).jpg` | 没观察 |
| 原始格式 | JPEG→JPG，PNG→PNG，TIFF→TIFF8，其余（BMP、RAW）→JPG；壳层按此定扩展名，exportd 按扩展名写 | 只看到选项存在 |
| TIFF16 | 8 位管线的结果展宽为 16 位（v×257）；文件是 16 位，精度仍是 8 位 | 只看到选项存在 |
| 品质档位 | 低 3、中 6、高 9、最佳 12；数值 ≥12 显示最佳、≥9 高、≥6 中 | 只看到 12 = 最佳 |
| 记住的默认值 | 设为默认导出路径、设为默认格式存在 `<数据目录>/exports/defaults` | — |

## 验证（2026-11-07，M2 Pro）

- `test_export_session`（真 exportd 进程）测了这些情况：
  - BMP 原图选“原始格式”，导出为 JPG，写到 `out/shoot/a.jpg`；
  - 再导一次，得到 `a(1).jpg`；
  - TIFF16 导到原图文件夹，`II/MM` 文件头正确；
  - 设文件大小限制时任务失败，原因带“文件大小”，重试后仍失败；
  - 重开会话后，4 条任务和失败原因都还在；
  - 默认值能记住；
  - 原图移走时得到提示；
  - 未登录时什么都不排队。
- `test_exportd`：JPEG 原图选原始格式得到 JPEG，PNG 原图得到 PNG；TIFF16 经 ImageIO 读回为每样本 16 位。
- `test_preview_protocol`：开始消息往返；原因字段往返；开始消息不能按进度消息解码。
- `test_export_queue`：`format=tiff16` 解析和编码。
- 壳层冒烟 `test_shell_app_import_flow` 加了导出一步，过程如下：
  1. 精修顶栏“导出”可用；
  2. 弹窗有 3 个页签，排版印刷置灰并带说明，水印置灰并带说明，格式 4 项；
  3. 切到自定设置，选 JPG、指定文件夹，点“导出”；
  4. exportd 写出 `…/Shoot 01/portrait-2.jpg`，导出列表显示“成功”；
  5. 成品平均红色 227.9，与精修画面的 228.0 一致（原图是 133.2）。成品带着精修的曝光。
- 截图（冒烟时设 `PIXCAKE_SMOKE_SCREENSHOT`）看过快速导出、自定设置（展开高级设置）和导出列表三张，都是深色、可读。
- 全量测试：默认构建 89/89，推理构建 91/91，Release 86/86；架构依赖检查和 `git diff --check` 都干净。

## 已知限制

- ~~导出到另一个卷（外接盘）会失败~~：已修，见下一节。
- 带蒙版的局部调整、水印照旧拒绝导出（原因写进导出列表）。
- 导出扣张数（账本 `settle_exports`）没有接。
- 快速导出页上的三个统计按钮（精修效果图、免费效果图、原图）没有做：它们和张数计费有关。
- 只导出当前选中的一张：图库还不能多选。
- exportd 和 renderd 一样从构建目录启动，.app 里还没有打包。

## 导出到外接盘（2026-11-07 修复）

**原来的问题**：发布用 `link()`，临时文件在 `<数据目录>/exports/cache`，不能跨卷。在挂载的磁盘镜像上实测：任务停在“提交到目标路径”，一直不结束。原因是发布失败时队列不记失败事件，exportd 随后记失败又被拒绝。

**修复**：
- `FileSystem::device_of` 查目录所在设备；`ExportQueue::temp_path_for` 按任务选临时文件的位置：
  - 缓存目录和目标文件夹在同一设备上，照旧放缓存目录；
  - 不在同一设备上（外接盘），写成目标文件夹里的隐藏文件 `.pixcake-<任务号>.part`。
- 这样发布仍是同卷的 `link()`，成品和临时文件是同一个文件，原有的归属校验（设备号 + inode）不变。
- 恢复时两个位置都找（盘可能已拔出，所以不按设备重新判断）：
  - 没写完的，两处都删，任务记为失败；
  - 已写完、没发布的，从找到的那一处发布。

**与架构的出入**：架构第 11 节写的是“目标目录没有半成品”。现在只在导出到别的卷时，写入过程中目标文件夹里会有一个隐藏的 `.part` 文件。崩溃的话它会留到下次启动，届时恢复删除。跨卷只能先把字节写到那个卷上再原子换名，这是能做到的最接近的方式。同卷导出不受影响。

**验证**：
- `test_export_to_another_device`：模拟第二个设备，提交成功，不留临时文件；同设备的任务仍写缓存目录。
- `test_crash_on_another_device`：
  - 写隐藏临时文件时崩溃，恢复后删除临时文件，任务记为失败；
  - 已写完、未发布时，恢复负责发布。
- 变异检查：把位置固定为缓存目录，上面两个测试失败。
- 真实第二个卷：在草稿目录挂了一个 32 MB 的 APFS 磁盘镜像（设备号和构建目录不同），然后设 `PIXCAKE_TEST_OTHER_VOLUME=<挂载点>` 跑 `test_export_session`，导出到镜像上的文件夹。
  - 修复后：`a.jpg`、`a(1).jpg` 都写到镜像上，没有遗留 `.part` 文件。
  - 去掉修复：任务一直卡在“提交到目标路径”，测试等待超时。
  - 镜像随后卸载并删除。
