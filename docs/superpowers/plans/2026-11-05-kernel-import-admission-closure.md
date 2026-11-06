# Kernel 导入准入边界闭环计划

日期：2026-11-05

## 背景和审计结论

上一份 `2026-11-05-kernel-generation-boundary-closure.md` 的 4 个任务已经完成，
当前全量测试为 36/36。整体 kernel 仍没有导入模块；复审中的 C-40、C-11、C-12、
B-34、B-40、B-90 和 S-07 不能用项目索引或文件扩展名代替内容验真。

本计划只落实导入的准入合同。桌面规格第 3.3 节明确支持 JPG/JPEG、PNG、TIF/TIFF、
BMP 和相机 RAW；本阶段对前四类做有界头部验真，对 RAW 只保留“由解码器配置提供
扩展名”的接口，不猜测 RAW 扩展名，也不实现 RAW 解码。模块只返回来源路径、格式、
尺寸和文件身份，不写项目库、不复制像素、不生成缩略图、不读取 XMP。

## 实现任务

### Task 1：冻结导入格式和结果模型 `[x]`

- 新增无 Qt、无 SQLite 的 `kernel/import` 公共类型。
- 固定 JPG/JPEG、PNG、TIF/TIFF、BMP 四类内容格式；扩展名只能作为显示提示，不能单独放行。
- 结果携带绝对路径、识别出的格式、宽高和 `FileIdentity`；不携带像素缓冲区。

### Task 2：实现有界头部验真 `[x]`

- 按文件签名和格式头读取尺寸，先检查偏移、长度、整数溢出，再返回宽高。
- 已知签名但头部截断或字段非法返回 `ImageCorrupt`；未知签名返回 `UnsupportedFormat`。
- 宽高必须大于零，像素面积计算溢出时拒绝；不把预览最长边 4000 当作解码上限。

### Task 3：实现文件准入和身份采集 `[x]`

- 区分路径不存在（`SourceMissing`）、权限错误、读错误、格式不支持和内容损坏。
- 只接受绝对路径；通过 `FileSystem::identify` 采集持久身份，供项目库后续绑定和删除校验。
- 单个文件失败不影响调用方继续处理同批其他文件；本模块不执行任何项目写入。

### Task 4：测试、门禁和证据 `[x]`

- 覆盖四类格式的有效头、截断头、零尺寸、非法尺寸和未知内容。
- 覆盖不存在、权限失败、读失败、相对路径和身份采集；确认不会写文件或复制像素。
- 新目标不链接 Qt、OpenGL、SQLite、网络或解码器；更新本计划和证据。

## 验收条件

- 有效 JPG/JPEG、PNG、TIF/TIFF、BMP 头部能得到正确格式和尺寸；扩展名与内容不一致时按内容处理。
- 已知格式损坏、零尺寸、宽高乘积溢出、未知格式和非法路径均返回明确结果码。
- 成功结果保留绝对路径和文件身份，不生成项目记录，不持有像素。
- 全量 CTest、架构依赖门禁和 `git diff --check` 通过。

## 实现证据

- `kernel/import/include/pixcake/image_import/source_probe.hpp` 冻结四类图片格式和准入结果模型。
- `kernel/import/src/source_probe.cpp` 按 PNG、JPEG、TIFF、BMP 头部验证尺寸，并采集 `FileIdentity`。
- `tests/kernel/import/test_source_probe.cpp` 覆盖有效头、截断头、零尺寸、未知内容、路径/权限和只读行为。
- 验证目录：`/private/tmp/pixcake-kernel-import-20261105`。
- 最终结果：37/37 CTest 通过，架构依赖门禁和 `git diff --check` 通过。

## 明确不在本阶段

RAW 解码器和扩展名清单下发、真实像素解码、EXIF/XMP、缩略图、项目库批量插入、
系统安全范围书签、远端服务和 UI 导入流程继续保持未完成状态。
