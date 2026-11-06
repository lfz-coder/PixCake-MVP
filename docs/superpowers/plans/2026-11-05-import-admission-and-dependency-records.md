# 导入准入凭证与依赖记录 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

日期：2026-11-05
状态：已完成
分支：`codex/desktop-official-layout`，基线 `714a87a`

**Goal:** 原图只能经内容准入后进入项目库（S-07）；手写的图像头探测有依赖规范要求的例外记录（S-33/Q-34）；Glad 有可追溯的依赖记录（S-34/Q-35）。

**Architecture:** `kernel/project` 新增不可伪造的 `AdmittedSource`：构造函数私有，只有 `kernel/import` 里的 `SourceAdmission` 能签发（passkey 模式，只用前置声明，不引入链接依赖）。`ProjectLibrary::add_photo` 改为接收 `AdmittedSource`，写入前重新识别文件身份，和准入时的身份比对，防止准入之后文件被替换。`ImportCoordinator` 改用 `admit_source`。依赖记录写进 `third_party/` 的 lock 文件和 `docs/superpowers/evidence/`。

**Tech Stack:** C++17、CMake/CTest。

**Spec:**
- 第三轮审核：S-07（`add_photo` 公开旁路、`not_found` 仍写入）、S-33/Q-34（`source_probe.cpp` 手写格式解析无例外记录）、S-34/Q-35（Glad 无依赖记录）
- `docs/superpowers/specs/2026-11-06-pixcake-third-party-dependency-policy.md` §2（引入记录六项）、§3（锁定与可追溯）、§4（手写例外记录）
- 契约 C-15（原文件缺失不得表示“这次导入就打不开”）、C-40；安全清单 S-07、S-10

## Global Constraints

- 没有经过 `admit_source` 的路径不能进项目库；`add_photo` 不再有“文件不存在也写入”的分支。
- 准入后、写入前文件身份（device、inode、size）发生变化：返回 `StateConflict`，不写入。
- `kernel/` 不引入图像库头文件（架构门禁 rule 15）；`kernel/project` 不链接 `pixcake_image_import`。
- 依赖记录只写能从仓库文件或官方文档确认的事实。许可证全文目前拿不到原文（本机没有，联网安装生成器也未获授权），记为发布前必须补齐的阻塞项，不凭记忆写。
- 测试名带清单编号。

## Review Focus

1. 准入后、写入前文件被替换成另一个文件（inode 变化）：必须拒绝。
2. 同一路径准入两次后写入两次：按项目库现有的重复规则处理，不能因为凭证被重复使用就绕开。
3. 相对路径、空路径：在准入阶段就被拒绝，项目库不会收到凭证。
4. 现有删除原图、虚拟副本、原图缺失扫描等测试改为经准入加图片后，断言含义不变。
5. 依赖记录与仓库里的实际文件一致（生成命令、版本、SPDX 标识逐字取自 `gl.h` 头部）。

---

## 文件

- Create: `kernel/project/include/pixcake/project/admitted_source.hpp`、`kernel/project/src/admitted_source.cpp`
- Modify: `kernel/project/include/pixcake/project/project_library.hpp`、`kernel/project/src/project_library.cpp`、`kernel/project/CMakeLists.txt`
- Modify: `kernel/import/include/pixcake/image_import/source_probe.hpp`、`kernel/import/src/source_probe.cpp`、`kernel/import/src/import_coordinator.cpp`
- Create: `tests/support/admitted_photo.hpp`（测试辅助：在 `FakeFileSystem` 写入最小 PNG 头并准入）
- Modify: 调用 `add_photo` 的 6 个测试文件、`tests/CMakeLists.txt`（相关测试链接 `pixcake_image_import`）
- Create: `third_party/glad.lock.cmake`、`docs/superpowers/evidence/2026-11-05-dependency-records.md`

### Task 1: 准入凭证

**Interfaces（Produces）:**
```cpp
// kernel/project
namespace pixcake {
namespace image_import { class SourceAdmission; }
class AdmittedSource {
 public:
  const std::string& absolute_path() const;
  const FileIdentity& identity() const;
 private:
  friend class image_import::SourceAdmission;
  AdmittedSource(std::string absolute_path, FileIdentity identity);
  std::string absolute_path_;
  FileIdentity identity_;
};
ProjectResult ProjectLibrary::add_photo(std::int64_t project_id, const AdmittedSource& source,
                                        std::string file_name, std::vector<std::uint8_t> access_credential);
}
// kernel/import
namespace pixcake::image_import {
ResultCode admit_source(FileSystem& files, const std::string& absolute_path,
                        std::optional<AdmittedSource>& out, SourceProbe* details = nullptr);
}
```
- `admit_source` = `probe_source` 成功后由 `SourceAdmission` 签发凭证；失败时 `out` 为空，结果码与 `probe_source` 相同。
- `add_photo`：重新 `identify`，任何读取失败原样返回，`not_found` 返回 `SourceMissing`；身份与凭证不一致返回 `StateConflict`。

- [x] **Step 1: 写失败测试**（`tests/kernel/import/test_import_coordinator.cpp`）：
  - `test_s07_admitted_source_is_required_to_add_photo`：`admit_source` 对 `{1,2,3}` 返回 `ImageCorrupt` 且 `out` 为空；对最小 PNG 返回 `Ok`；用凭证 `add_photo` 成功。
  - `test_s07_replaced_file_after_admission_is_rejected`：准入后修改 `FakeFileSystem` 中该文件的身份（换 inode），`add_photo` 返回 `StateConflict`，项目里没有这张图。
  - `test_c15_missing_file_after_admission_is_not_added`：准入后删除文件，`add_photo` 返回 `SourceMissing`，没有写入。
- [x] **Step 2:** 编译失败（类型不存在）。
- [x] **Step 3:** 实现；把 6 个测试文件里的 `add_photo(pid, "<path>", …)` 改为 `add_photo(pid, admitted_png(files, "<path>"), …)`；原先断言相对路径被 `add_photo` 拒绝的用例，改为断言 `admit_source` 拒绝。
- [x] **Step 4:** 全量 CTest 通过；`arch_dependencies` 通过。

### Task 2: 依赖记录

- [x] **Step 1:** 写 `third_party/glad.lock.cmake`：生成器 glad 2.0.8、`--api='gl:core=3.3' --extensions='' c --loader`、生成时间、SPDX 表达式、三个文件的 SHA-256（用 `shasum -a 256` 现场计算），以及“许可证全文待补”的阻塞标记。
- [x] **Step 2:** 写证据 `docs/superpowers/evidence/2026-11-05-dependency-records.md`：
  - Glad：按依赖规范 §2 六项逐项记录（来源与生成命令、平台、许可证与随包义务、构建方式、安全与输入、与项目结果码的映射）；复现比对和许可证全文是发布前阻塞项。
  - `source_probe` 手写例外（规范 §4）：考虑过的候选（libvips、OpenImageIO、LibRaw 来自规范 §5）、排除理由（架构门禁 rule 15 禁止 `kernel/` 引入图像库头；本函数只读签名和尺寸，不解码像素）、范围、测试向量（`test_source_probe` 现有用例）、损坏输入处理、替换路径（`apps/renderd` 的 `ImageDecoder` 接入 libvips 后，由解码适配器做完整内容验证，内核探测只作快速预筛）。
- [x] **Step 3:** 验证 lock 文件中的 SHA-256 与 `shasum` 输出一致。

### Task 3: 收尾

- [x] **Step 1:** 更新 `project_library.hpp` 顶部注释（“add_photo 不探测文件”已不成立）。
- [x] **Step 2:** 全量构建、CTest、`git diff --check`；勾选计划；提交 `feat(kernel): require admitted sources for photos and record dependencies`。
