# 复审记录：2026-11-04，否决项修复

本记录对应 `2026-11-04-pixcake-current-review.md` 的修复工作。它只确认已经落地并自动化验证的边界，不把尚未存在的导入、渲染、生成和 IPC 模块写成已完成。

## 已关闭的具体问题

| 问题 | 修复 | 自动化证据 |
| --- | --- | --- |
| F07 / B-63 / B-65 / B-73 | `ExportBook::Sink::persist` 在同一 SQLite 事务内重读事件并校验状态迁移；旧句柄不能在取消后提交 | `test_b63_stale_export_handle_cannot_publish_after_cancel` |
| F01 / S-09 / B-98 | 导出临时文件记录设备号、inode、大小；恢复同时核对校验和与持久身份，外部同内容临时文件不会被认领 | `test_b98_recovery_rejects_unowned_identical_temp_file` |
| F02 / C-50 | POSIX 发布使用不覆盖目标的 `link` + `unlink`，目标碰撞保留原文件并返回状态冲突 | `test_publish_does_not_overwrite_a_file_that_appears_after_preflight` |
| B-111 / O-21 | 原子替换回滚增加重试；持续换名失败时从 `.bak` 重建正式文件，并保留备份作为下一次恢复源 | `test_b111_transient_restore_failure_retries_and_preserves_formal`、`test_b111_permanent_restore_failure_recreates_formal_from_backup` |
| F05 / C-20 / C-47 | 大师精选资格在加速扣费前判断，三次免费额度耗尽时不扣点 | `test_master_acceleration_does_not_bypass_subscription` |
| B-04 / B-20 / S-06 | 预设名称拒绝完整 Unicode 空白、非法 UTF-8，并按可见字符计数 | `test_c44_name_counts_characters_not_bytes` |
| C-36–C-39 / B-27 | 色温、色调、曝光、影调参数使用统一范围和步进校验；未知参数拒绝写入 | `test_capability_registry`、`test_edit_document` |
| C-21 / P-21 | 预设应用 API 必须显式接收极速导出门闩；门闩开启时返回 `FastExportBusy`，删除无门闩旁路 | `test_preset_library` |
| C-01 | 新建工作区默认打开 RAW、修图状态、导出状态三个显示项 | `test_workspace_display` |
| P-11 / O-17 | 临时文件清理失败持久化为「待清理临时文件」，恢复重试清理成功后才转为失败态 | `test_p11_failed_temp_cleanup_remains_retryable_after_reopen` |
| P-21 / O-22 | 账本和导出队列打开时在同一持久事务内读取事件、核对缓存并修复缓存，避免跨句柄读写窗口 | `test_ledger`、`test_export_queue` |

## 验证

```text
cmake -S . -B /private/tmp/pixcake-review-current2 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-review-current2 --parallel 8
ctest --test-dir /private/tmp/pixcake-review-current2 --output-on-failure
git diff --check
```

结果：**29/29 CTest 通过**，架构依赖门禁和能力归属门禁均通过，`git diff --check` 通过。

## 仍未实现的范围

原复审中标为“未实现”的导入解码、配置服务、渲染/生成进程、IPC、kernel-cli、单实例、真实日志指标和完整 UI 入口仍保持未实现状态。它们不能由本次内核边界修复或静态 smoke 测试替代，下一轮应按各自清单编号继续落地。
