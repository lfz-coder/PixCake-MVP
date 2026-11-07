# 2026-11-06 边界清单用例覆盖

用途：第七轮条件 4 中的 T-02、T-09（每条 B 编号至少一条带编号的用例）。统计口径：`tests/` 下 `void test_*` 函数名里的 `_bNN`。

| 编号 | 场景 | 用例或原因 |
| --- | --- | --- |
| B-01 | 命令缺命令名、缺目标，或提交时没有修订号 | `test_b01_missing_fields_do_not_write`（kernel/command/test_command_gate.cpp） |
| B-02 | 导入列表为空 | `test_b02_b34_b90_import_list_and_batch_results`（kernel/import/test_import_coordinator.cpp） |
| B-03 | 导出时没有选中图片 | `test_b03_export_without_selected_image_is_refused`（kernel/exportq/test_export_queue.cpp） |
| B-04 | 预设名为空或只有空白 | `test_c44_b04_b20_b21_name_counts_characters_not_bytes`（kernel/preset/test_preset_library.cpp） |
| B-05 | 搜索关键字为空 | 未实现：项目搜索没有关键字查询（`PhotoQuery` 没有关键字字段） |
| B-06 | 验证码为空、不足 6 位或超过 6 位；手机号为空 | `test_o13_b06_b50_b82_login_failures_are_distinct_and_stay_on_login`（services/account/test_account_session.cpp） |
| B-07 | 没有配置缓存 | `test_b07_b86_cache_fallback_and_replacement`（services/config/test_config_store.cpp） |
| B-08 | 参数文件损坏且没有备份 | `test_b08_posix_truncate_without_backup_is_marked_empty`（recovery/test_document_recovery.cpp）；`test_b08_no_backup_marks_recovered_empty_and_does_not_write_formal`（recovery/test_persist_recovery.cpp） |
| B-09 | 蒙版编号、素材编号在文档里，但目标已经不存在 | `test_b09_types_and_missing_file_semantics`（kernel/assets/test_asset_catalog.cpp） |
| B-10 | 人物检测还没完成 | `test_c31_b10_portrait_needs_detection`（kernel/capability/test_capability_modules.cpp）；`test_b10_pending_and_failed_do_not_expose_geometry`（kernel/subject/test_subject_store.cpp） |
| B-11 | 本机没有打印机 | `test_b11_b53_cancel_removes_temp_and_print_is_unavailable`（kernel/exportq/test_export_queue.cpp） |
| B-12 | 未登录 | `test_s25_b12_signed_out_document_commands_rejected`（kernel/command/test_photo_status.cpp）；`test_s25_b12_signed_out_business_commands_rejected`（kernel/command/test_photo_status.cpp） |
| B-13 | 需要提示词的生成工具收到空提示词，且该工具的参数模式没有写明允许空 | 未实现：生成任务没有提示词字段 |
| B-14 | 项目里 0 张照片时触发挑图、人均计算或整项目导出 | 未实现：挑图会话不读项目照片；人均计算与整项目导出入口还没接 |
| B-15 | 筛选、排序、标记作用于空选择 | `test_b15_b28_marks_accept_only_the_documented_values`（kernel/project/test_photo_marks.cpp） |
| B-20 | 预设名正好 20 个用户可见字符 | `test_c44_b04_b20_b21_name_counts_characters_not_bytes`（kernel/preset/test_preset_library.cpp） |
| B-21 | 预设名 21 个字符，或工具栏第 6 个，或收藏色第 7 个 | `test_c44_b04_b20_b21_name_counts_characters_not_bytes`（kernel/preset/test_preset_library.cpp）；`test_b21_layout_limits_and_logout_scope`（services/account/test_account_session.cpp） |
| B-22 | 样片库已有 2000 张，或单次导入超过 1000 张 | `test_b22_sample_batch_limits_are_atomic`（kernel/assets/test_asset_catalog.cpp） |
| B-23 | 证件照「我的背景」不是 jpg、png、tif、bmp，或单张超过 10MB | 未实现：证件照“我的背景”素材导入 |
| B-24 | 面部重绘超过 5 人 | 未实现：按人数报价（面部重绘）；账本只接收已算好的报价 |
| B-25 | 证件照换衣服 1 人或 2 人 | 未实现：按人数报价（证件照换衣服） |
| B-26 | 证件照换衣服 3 人及以上 | 未实现：按人数报价（证件照换衣服） |
| B-27 | 契约清单里已有范围的滑杆，拖动或输入超出两端 | `test_c38_b27_exposure_spec_comes_from_parameter_mode`（ui/test_slider_spec.cpp） |
| B-28 | 星级不是 0 到 5，旗标不是三态之一，色标不是红黄绿蓝紫 | `test_b15_b28_marks_accept_only_the_documented_values`（kernel/project/test_photo_marks.cpp） |
| B-29 | 历史步骤、图层、蒙版子项的下标越界 | `test_b29_bad_index_keeps_revision_and_neighbors`（kernel/history/test_history.cpp） |
| B-30 | 删除未满 30 天，以及刚好到期 | `test_b30_under_thirty_days_can_be_restored`（kernel/project/test_recycle_bin.cpp）；`test_b30_expired_index_is_removed_without_deleting_the_file`（kernel/project/test_recycle_bin.cpp） |
| B-31 | 图库网格每行被调到 0 或大于 7 | `test_b31_library_controls`（ui/test_shell_display.cpp） |
| B-32 | 导出品质不是 0 到 14 的整数 | `test_b32_b47_export_settings_parse_strictly`（kernel/exportq/test_export_queue.cpp）；`test_b32_enqueue_rejects_duplicate_quality`（kernel/exportq/test_export_queue.cpp）；`test_b32_enqueue_stores_canonical_settings`（kernel/exportq/test_export_queue.cpp） |
| B-33 | 不透明度不是 0 到 100，蒙版羽化不是 0 到 100，祛路人选区羽化不是 0 到 1000，裁剪角度不是 -45 到 45，追色程度不是 0 到 200，色彩风格程度不是 0 到 100（2026-11-05 用户裁定） | `test_b33_unconfirmed_step_checks_range_only`（kernel/capability/test_capability_registry.cpp）；`test_b33_c48_amount_ranges`（kernel/capability/test_capability_registry.cpp）；`test_b33_unconfirmed_step_and_default_are_labelled`（ui/test_slider_spec.cpp） |
| B-34 | 导入批次中间某张损坏、无权限或格式不支持 | `test_b02_b34_b90_import_list_and_batch_results`（kernel/import/test_import_coordinator.cpp） |
| B-35 | 照片数是 149 和 150 | 未实现：挑图 150 张询问阈值 |
| B-36 | 全图蒙版被隐藏、删除或编辑 | 未实现：全图蒙版的隐藏、删除、编辑命令（`kernel/mask`） |
| B-37 | 画笔蒙版进入预设或批量同步；局部追色被批量同步 | `test_b37_checked_sync_copies_only_selected_fragments`（kernel/sync/test_sync.cpp） |
| B-40 | 图片宽或高为 0、负数，或宽高相乘溢出 | `test_b40_b90_bad_headers_are_rejected`（kernel/import/test_source_probe.cpp） |
| B-41 | 裁剪目标的宽或高为 0 | 未实现：裁剪目标尺寸 |
| B-42 | 排版行数或列数为 0 | 未实现：排版 |
| B-43 | 预览缩放比为 0 或负数 | 未实现：画布预览缩放（画布未接） |
| B-44 | 曝光、色温的内部换算 | 待补：曝光、色温的内部换算在渲染端，没有内核层断言 |
| B-45 | 面部重绘按人扣点，但有效人数为 0 | 未实现：按人数报价，有效人数为 0 的判断 |
| B-46 | 直方图或取色读到空缓冲 | 未实现：直方图、取色读像素 |
| B-47 | 导出文件大小限制为 0 或负数 | `test_b32_b47_export_settings_parse_strictly`（kernel/exportq/test_export_queue.cpp） |
| B-48 | 共享内存帧的宽、高、行距为 0、负数，或与字节数不符 | `test_b48_cancel_and_frame_header_reject_bad_values`（kernel/ipc/test_preview_protocol.cpp） |
| B-50 | 登录、短信、生成、云传输、检查更新超时或断网 | `test_t03_b50_remote_timeout_fails_with_network_failed`（apps/test_generated.cpp）；`test_o13_b06_b50_b82_login_failures_are_distinct_and_stay_on_login`（services/account/test_account_session.cpp） |
| B-51 | 等待发生在主线程的滑杆拖动、滚动或窗口缩放里 | 未实现：真实画布与主线程交互（第 4 关） |
| B-52 | 已提交命令后超过 2 秒仍没写完 | `test_b52_commit_slower_than_two_seconds_is_not_success`（kernel/editdoc/test_edit_document.cpp）；`test_b52_late_write_keeps_formal_file_and_is_not_success`（kernel/persist/test_atomic_file.cpp） |
| B-53 | 生成或导出被用户取消 | `test_b11_b53_cancel_removes_temp_and_print_is_unavailable`（kernel/exportq/test_export_queue.cpp） |
| B-54 | 第三方直到超时都没有返回，且还没有扣点事件 | 待补：真实传输接入后的“超时与扣点事件先后”；当前替身的超时发生在已扣点之后 |
| B-55 | 第三方超时，但扣点事件已经写下 | 待补：同上 |
| B-60 | 同一份文档的两个写入同时到达 | `test_b60_simultaneous_commits_serialize_to_one_revision`（kernel/command/test_command_gate.cpp）；`test_b60_two_gates_share_document_submission_state`（kernel/command/test_command_gate.cpp）；`test_b60_c23_cross_process_gate_rejects_stale_commit`（kernel/command/test_command_gate.cpp） |
| B-61 | 拖动中的预览副本和松手提交叠在一起 | `test_b61_commit_writes_latest_preview`（kernel/command/test_command_gate.cpp）；`test_b61_commit_keeps_latest_display_value`（kernel/editdoc/test_edit_document.cpp） |
| B-62 | 极速导出开着，同时还有编辑或预览命令；之后队列变空 | `test_b62_fast_export_stays_until_the_switch_is_turned_off`（kernel/command/test_command_gate.cpp）；`test_b62_other_process_sees_fast_export`（storage/test_sqlite_fast_export_latch.cpp）；`test_b62_empty_queue_keeps_fast_export`（storage/test_sqlite_fast_export_latch.cpp）；`test_b62_unreadable_log_fails_closed`（storage/test_sqlite_fast_export_latch.cpp）；`test_b62_missing_database_is_reported`（storage/test_sqlite_fast_export_latch.cpp） |
| B-63 | 同一任务编号重复提交，或恢复逻辑重放 | `test_b63_stale_export_handle_cannot_publish_after_cancel`（kernel/exportq/test_export_queue.cpp） |
| B-64 | 同一笔触编号的自动消除被重放 | `test_b64_accelerate_is_charged_once_and_strokes_dedupe`（kernel/ledger/test_ledger.cpp） |
| B-65 | 结果晚于取消或失败到达 | `test_c30_b65_b81_cancel_while_running_discards_late_result`（apps/test_generated.cpp） |
| B-66 | 导出已入队，用户继续改已提交参数 | `test_b66_snapshot_and_fast_export_stay`（kernel/exportq/test_export_queue.cpp） |
| B-67 | 第二个进程启动，或两处同时打开数据库 | `test_b67_second_claim_sees_running_primary`（services/instance/test_single_instance.cpp）；`test_b67_long_socket_path_is_rejected`（services/instance/test_single_instance.cpp） |
| B-68 | 批量同步、批量导入或批量导出中一项失败 | `test_b68_same_path_skips_only_that_target`（kernel/sync/test_sync.cpp） |
| B-69 | 主进程在结果已就绪后、落层前结束 | 未实现：主进程落层（结果就绪后写图层文档） |
| B-70 | 批量设计进行中，又来了编辑、历史或图层命令 | `test_b70_batch_design_rejects_history_commands`（kernel/history/test_history.cpp） |
| B-71 | 模态层打开时，文件被拖到应用图标 | `test_b71_icon_drop_follows_the_page`（tests/ui/test_shell_display.cpp）；`test_shell_app_import_flow` 用 `QFileOpenEvent` 走精修追加、模态层不导入、主页新建三种情况；真实 LaunchServices 打开文档事件见 2026-11-06-renderd-transport-and-decoder.md §5.3。拖到程序坞图标本身仍是人工步骤（测试清单第 34 行） |
| B-72 | 生成进程崩溃，与主进程处理事件同时发生 | 未实现：生成进程崩溃与主进程处理事件并发 |
| B-73 | 旧修订号的命令晚到 | `test_b73_stale_revision_does_not_write`（kernel/command/test_command_gate.cpp）；`test_c23_b73_failed_document_commands_do_not_notify`（kernel/command/test_command_gate.cpp）；`test_b73_stale_revision_does_not_overwrite`（kernel/editdoc/test_edit_document.cpp）；`test_c23_b73_stale_commit_preview_is_rejected`（kernel/editdoc/test_edit_document.cpp）；`test_c23_b73_stale_fragment_replacement_is_rejected`（kernel/editdoc/test_edit_document.cpp）；`test_c23_b73_stale_baked_reset_is_rejected`（kernel/editdoc/test_edit_document.cpp）；`test_b73_reloaded_handle_commits_next_revision`（kernel/editdoc/test_edit_document.cpp）；`test_b73_older_revision_does_not_append`（kernel/history/test_history.cpp）；`test_c23_b73_stale_layer_save_is_rejected`（kernel/layerdoc/test_layer_document.cpp） |
| B-74 | 渲染进程只收到旧的预览值，更新的值已经在队列里 | `test_b74_latest_value_wins_without_writing`（kernel/preview/test_preview_coordinator.cpp） |
| B-80 | 返回空、不是图片、声明尺寸和字节数不符，或校验失败 | `test_t03_b80_remote_bad_return_fails_without_file`（apps/test_generated.cpp）；`test_t03_b80_remote_size_mismatch_fails_and_keeps_previous_result`（apps/test_generated.cpp） |
| B-81 | 返回看起来成功，但本地任务已经是取消或失败 | `test_c30_b65_b81_cancel_while_running_discards_late_result`（apps/test_generated.cpp） |
| B-82 | 验证码错误、过期或未收到 | `test_o13_b06_b50_b82_login_failures_are_distinct_and_stay_on_login`（services/account/test_account_session.cpp） |
| B-83 | 预设口令来自更高版本，或分享者已删除 | 未实现：预设分享口令 |
| B-84 | 片段版本高到不能迁移 | `test_b84_too_new_fragment_is_not_rewritten`（kernel/editdoc/test_edit_document.cpp） |
| B-85 | 片段里有当前版本不认识的字段 | `test_b85_unknown_inner_field_survives_commit`（kernel/editdoc/test_edit_document.cpp） |
| B-86 | 下发配置缺字段或类型不对 | `test_b07_b86_cache_fallback_and_replacement`（services/config/test_config_store.cpp） |
| B-90 | 导入的文件损坏或格式不支持 | `test_b02_b34_b90_import_list_and_batch_results`（kernel/import/test_import_coordinator.cpp）；`test_b40_b90_bad_headers_are_rejected`（kernel/import/test_source_probe.cpp） |
| B-91 | 已经在项目里的原图后来损坏 | 未实现：已入项目的原图后来损坏的重新识别 |
| B-92 | 原图被移走或改名 | `test_b92_checksum_ignores_unrelated_source_path`（kernel/editdoc/test_edit_document.cpp） |
| B-93 | 自动保存时磁盘满或没有写权限 | `test_b93_disk_full_does_not_advance_revision`（kernel/editdoc/test_edit_document.cpp）；`test_b93_failed_layer_save_keeps_revision`（kernel/layerdoc/test_layer_document.cpp）；`test_b93_disk_full_keeps_formal_file`（kernel/persist/test_atomic_file.cpp）；`test_b93_permission_denied_keeps_formal_file`（kernel/persist/test_atomic_file.cpp） |
| B-94 | 导出时磁盘满 | `test_b94_disk_full_then_retry_uses_the_snapshot`（kernel/exportq/test_export_queue.cpp） |
| B-95 | 导出崩溃：还没开始写临时文件 | `test_b95_b96_b97_rename_failure_and_crash_points`（recovery/test_export_recovery.cpp） |
| B-96 | 导出崩溃：正在写临时文件 | `test_b95_b96_b97_rename_failure_and_crash_points`（recovery/test_export_recovery.cpp） |
| B-97 | 导出崩溃：临时文件已完整，还没换名 | `test_b95_b96_b97_rename_failure_and_crash_points`（recovery/test_export_recovery.cpp） |
| B-98 | 导出崩溃：目标文件已通过校验 | `test_b98_recovery_rejects_unowned_identical_temp_file`（recovery/test_export_recovery.cpp） |
| B-99 | 目标路径上出现不完整成品 | `test_b99_publish_does_not_overwrite_a_file_that_appears_after_preflight`（kernel/exportq/test_export_queue.cpp） |
| B-100 | 参数文件被截断或校验失败 | `test_b100_posix_truncate_restores_backup_document`（recovery/test_document_recovery.cpp）；`test_b100_bad_file_is_isolated_and_backup_restored`（recovery/test_persist_recovery.cpp） |
| B-101 | 强制退出后重新打开 | `test_b101_reopen_folds_to_last_save`（recovery/test_history_restart.cpp） |
| B-102 | 从磁盘删除时没有访达或系统权限 | `test_b102_b112_delete_source_file`（kernel/project/test_recycle_bin.cpp）；`test_b102_delete_source_file_recovers_when_final_presence_write_fails`（recovery/test_source_delete_recovery.cpp） |
| B-103 | 排队中取消；生成中取消 | `test_b103_balances_stay_separate_and_queue_cancel_is_free`（kernel/ledger/test_ledger.cpp）；`test_b103_b105_generating_cancel_and_crash_do_not_refund`（recovery/test_ledger_replay.cpp） |
| B-104 | 生成崩溃：排队中尚未扣点 | `test_b104_b106_crash_folds_only_generating_tasks`（apps/test_generated.cpp） |
| B-105 | 生成崩溃：已扣点，结果未确认 | `test_c30_t03_b105_generated_crash_fails_without_refund`（apps/test_generated.cpp）；`test_b103_b105_generating_cancel_and_crash_do_not_refund`（recovery/test_ledger_replay.cpp） |
| B-106 | 生成崩溃：结果文件已写完且事件已确认 | `test_b104_b106_crash_folds_only_generating_tasks`（apps/test_generated.cpp） |
| B-107 | 纯色背景功能用于非纯色图 | `test_c31_b107_solid_background_parameter_has_no_effect_on_other_backgrounds`（kernel/capability/test_capability_modules.cpp） |
| B-108 | 全身美型判断会坏片 | `test_c31_b108_only_body_reports_broken_image`（kernel/capability/test_capability_modules.cpp） |
| B-109 | 原图缺失或导出失败，且没有成品 | `test_b109_b110_export_sheets_count_originals_once_per_settlement`（kernel/ledger/test_ledger.cpp） |
| B-110 | 同一原图的虚拟副本，以及同时导出的无水印第二份 | `test_b110_product_facts_match_one_settlement`（kernel/exportq/test_export_queue.cpp）；`test_b109_b110_export_sheets_count_originals_once_per_settlement`（kernel/ledger/test_ledger.cpp）；`test_b110_shares_original_without_a_charge`（kernel/project/test_virtual_copy.cpp） |
| B-111 | 写盘顺序的任一步失败 | `test_b111_crash_during_replace_reopens_last_complete_document`（recovery/test_document_recovery.cpp）；`test_b111_crash_after_backup_rename_reopens_backup`（recovery/test_persist_recovery.cpp）；`test_b111_transient_restore_failure_retries_and_preserves_formal`（recovery/test_persist_recovery.cpp）；`test_b111_permanent_restore_failure_recreates_formal_from_backup`（recovery/test_persist_recovery.cpp） |
| B-112 | 恢复项目；只恢复其中部分图片；删除原图 | `test_b112_restore_project_brings_back_its_photos`（kernel/project/test_recycle_bin.cpp）；`test_b112_partial_restore_leaves_the_other_photo`（kernel/project/test_recycle_bin.cpp）；`test_b102_b112_delete_source_file`（kernel/project/test_recycle_bin.cpp） |
| B-113 | 退出时导出或生成仍在进行 | `test_b113_reopen_repairs_cache_and_finishes_rename`（recovery/test_export_recovery.cpp） |
| B-114 | 更新应用后打开原来的项目库 | `test_b114_version_two_gains_default_marks`（kernel/project/test_photo_marks.cpp）；`test_b114_version_one_library_keeps_photos_and_gains_copies`（kernel/project/test_virtual_copy.cpp） |
| B-115 | 刷盘、换名或读写返回了空间和权限以外的系统错误 | `test_b115_directory_sync_failure_rolls_back`（kernel/persist/test_atomic_file.cpp）；`test_b115_first_save_directory_sync_failure_leaves_no_formal_file`（kernel/persist/test_atomic_file.cpp） |
| B-116 | 精修文档的字节布局。上位文档只规定了信封里有哪些事实，没有规定字节怎么排 | `test_b116_layer_file_without_revision_record_loads_as_zero`（kernel/layerdoc/test_layer_document.cpp） |
| B-117 | 预训练权重没有某个部位的单独输出 | 不在本计划：语义分割（另一会话） |

合计：98 条 B 行，74 条有带编号的用例，24 条未实现、待补或人工步骤（逐条写在表里）。
