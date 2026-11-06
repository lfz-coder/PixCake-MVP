add_executable(test_support kernel/support/test_support.cpp)
target_link_libraries(test_support PRIVATE pixcake_support pixcake_warnings)
add_test(NAME test_support COMMAND test_support)

add_executable(test_atomic_file kernel/persist/test_atomic_file.cpp)
target_include_directories(test_atomic_file PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_atomic_file PRIVATE pixcake_persist pixcake_warnings)
add_test(NAME test_atomic_file COMMAND test_atomic_file)

add_executable(test_edit_document kernel/editdoc/test_edit_document.cpp)
target_include_directories(test_edit_document PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_edit_document PRIVATE pixcake_editdoc pixcake_warnings)
add_test(NAME test_edit_document COMMAND test_edit_document)

add_executable(test_layer_document kernel/layerdoc/test_layer_document.cpp)
target_include_directories(test_layer_document PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_layer_document PRIVATE pixcake_layerdoc pixcake_editdoc pixcake_warnings)
add_test(NAME test_layer_document COMMAND test_layer_document)


add_executable(test_shell_display ui/test_shell_display.cpp)
target_link_libraries(test_shell_display PRIVATE pixcake_ui_shell pixcake_warnings)
add_test(NAME test_shell_display COMMAND test_shell_display)

add_executable(test_color_adjustment_panel ui/test_color_adjustment_panel.cpp)
target_link_libraries(test_color_adjustment_panel PRIVATE pixcake_ui_shell pixcake_warnings)
add_test(NAME test_color_adjustment_panel COMMAND test_color_adjustment_panel)

add_executable(test_slider_spec ui/test_slider_spec.cpp)
target_link_libraries(test_slider_spec PRIVATE pixcake_ui_panels pixcake_capability pixcake_warnings)
add_test(NAME test_slider_spec COMMAND test_slider_spec)

add_executable(test_guide_copy kernel/workspace/test_guide_copy.cpp)
target_link_libraries(test_guide_copy PRIVATE pixcake_workspace pixcake_warnings)
add_test(NAME test_guide_copy COMMAND test_guide_copy)

add_executable(test_copy_committed_document kernel/editdoc/test_copy_committed_document.cpp)
target_include_directories(test_copy_committed_document PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_copy_committed_document PRIVATE pixcake_image_import pixcake_editdoc pixcake_storage pixcake_warnings)
add_test(NAME test_copy_committed_document COMMAND test_copy_committed_document)

add_executable(test_document_recovery recovery/test_document_recovery.cpp)
target_include_directories(test_document_recovery PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_document_recovery PRIVATE pixcake_editdoc pixcake_warnings)
add_test(NAME test_document_recovery COMMAND test_document_recovery)

add_executable(test_persist_recovery recovery/test_persist_recovery.cpp)
target_include_directories(test_persist_recovery PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_persist_recovery PRIVATE pixcake_persist pixcake_warnings)
add_test(NAME test_persist_recovery COMMAND test_persist_recovery)

add_executable(test_export_recovery recovery/test_export_recovery.cpp)
target_include_directories(test_export_recovery PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_export_recovery PRIVATE pixcake_exportq pixcake_storage pixcake_command pixcake_ledger pixcake_warnings)
add_test(NAME test_export_recovery COMMAND test_export_recovery)

add_executable(test_ledger_replay recovery/test_ledger_replay.cpp)
target_include_directories(test_ledger_replay PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_ledger_replay PRIVATE pixcake_ledger pixcake_storage pixcake_warnings)
add_test(NAME test_ledger_replay COMMAND test_ledger_replay)

add_executable(test_layer_recovery recovery/test_layer_recovery.cpp)
target_include_directories(test_layer_recovery PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_layer_recovery PRIVATE pixcake_layerdoc pixcake_editdoc pixcake_warnings)
add_test(NAME test_layer_recovery COMMAND test_layer_recovery)

add_executable(test_manual_replay recovery/test_manual_replay.cpp)
target_link_libraries(test_manual_replay PRIVATE pixcake_manual pixcake_warnings)
add_test(NAME test_manual_replay COMMAND test_manual_replay)

add_executable(test_source_delete_recovery recovery/test_source_delete_recovery.cpp)
target_include_directories(test_source_delete_recovery PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_source_delete_recovery PRIVATE pixcake_image_import pixcake_storage pixcake_warnings)
add_test(NAME test_source_delete_recovery COMMAND test_source_delete_recovery)

add_executable(test_history_restart recovery/test_history_restart.cpp)
target_include_directories(test_history_restart PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_history_restart PRIVATE pixcake_history pixcake_warnings)
add_test(NAME test_history_restart COMMAND test_history_restart)

add_executable(test_sync kernel/sync/test_sync.cpp)
target_include_directories(test_sync PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_sync PRIVATE pixcake_sync pixcake_warnings)
add_test(NAME test_sync COMMAND test_sync)

add_executable(test_capability_registry kernel/capability/test_capability_registry.cpp)
target_link_libraries(test_capability_registry PRIVATE pixcake_capability pixcake_warnings)
add_test(NAME test_capability_registry COMMAND test_capability_registry)
add_executable(test_capability_modules kernel/capability/test_capability_modules.cpp)
target_link_libraries(test_capability_modules PRIVATE pixcake_capability_modules pixcake_warnings)
target_compile_definitions(test_capability_modules PRIVATE PIXCAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME test_capability_modules COMMAND test_capability_modules)

add_executable(test_render_description kernel/render/test_render_description.cpp)
target_include_directories(test_render_description PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_render_description PRIVATE pixcake_render pixcake_warnings)
add_test(NAME test_render_description COMMAND test_render_description)

add_executable(test_preview_coordinator kernel/preview/test_preview_coordinator.cpp)
target_include_directories(test_preview_coordinator PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_preview_coordinator PRIVATE pixcake_preview pixcake_warnings)
add_test(NAME test_preview_coordinator COMMAND test_preview_coordinator)

add_executable(test_preview_protocol kernel/ipc/test_preview_protocol.cpp)
target_link_libraries(test_preview_protocol PRIVATE pixcake_ipc pixcake_warnings)
add_test(NAME test_preview_protocol COMMAND test_preview_protocol)

add_executable(test_generation_task kernel/generate/test_generation_task.cpp)
target_link_libraries(test_generation_task PRIVATE pixcake_generate pixcake_warnings)
add_test(NAME test_generation_task COMMAND test_generation_task)

add_executable(test_source_probe kernel/import/test_source_probe.cpp)
target_include_directories(test_source_probe PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_source_probe PRIVATE pixcake_image_import pixcake_warnings)
add_test(NAME test_source_probe COMMAND test_source_probe)

add_executable(test_import_coordinator kernel/import/test_import_coordinator.cpp)
target_include_directories(test_import_coordinator PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_import_coordinator PRIVATE pixcake_image_import pixcake_storage pixcake_warnings)
add_test(NAME test_import_coordinator COMMAND test_import_coordinator)

add_executable(test_manual_stroke_session kernel/manual/test_manual_stroke_session.cpp)
target_link_libraries(test_manual_stroke_session PRIVATE pixcake_manual pixcake_warnings)
add_test(NAME test_manual_stroke_session COMMAND test_manual_stroke_session)

add_executable(test_subject_store kernel/subject/test_subject_store.cpp)
target_link_libraries(test_subject_store PRIVATE pixcake_subject pixcake_warnings)
add_test(NAME test_subject_store COMMAND test_subject_store)

add_executable(test_config_store services/config/test_config_store.cpp)
target_link_libraries(test_config_store PRIVATE pixcake_config pixcake_warnings)
add_test(NAME test_config_store COMMAND test_config_store)

add_executable(test_account_session services/account/test_account_session.cpp)
target_link_libraries(test_account_session PRIVATE pixcake_account pixcake_warnings)
add_test(NAME test_account_session COMMAND test_account_session)

add_executable(test_recipe kernel/recipe/test_recipe.cpp)
target_link_libraries(test_recipe PRIVATE pixcake_recipe pixcake_warnings)
add_test(NAME test_recipe COMMAND test_recipe)

add_executable(test_asset_catalog kernel/assets/test_asset_catalog.cpp)
target_link_libraries(test_asset_catalog PRIVATE pixcake_assets pixcake_warnings)
add_test(NAME test_asset_catalog COMMAND test_asset_catalog)

add_executable(test_cull_session kernel/cull/test_cull_session.cpp)
target_link_libraries(test_cull_session PRIVATE pixcake_cull pixcake_warnings)
add_test(NAME test_cull_session COMMAND test_cull_session)

add_executable(test_ledger kernel/ledger/test_ledger.cpp)
target_include_directories(test_ledger PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_ledger PRIVATE pixcake_ledger pixcake_storage pixcake_warnings)
add_test(NAME test_ledger COMMAND test_ledger)

add_executable(test_export_queue kernel/exportq/test_export_queue.cpp)
target_include_directories(test_export_queue PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_export_queue PRIVATE pixcake_exportq pixcake_storage pixcake_command pixcake_ledger pixcake_warnings)
add_test(NAME test_export_queue COMMAND test_export_queue)

add_executable(test_preset_library kernel/preset/test_preset_library.cpp)
target_include_directories(test_preset_library PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_preset_library PRIVATE pixcake_preset pixcake_warnings)
add_test(NAME test_preset_library COMMAND test_preset_library)

add_executable(test_history kernel/history/test_history.cpp)
target_include_directories(test_history PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_history PRIVATE pixcake_history pixcake_warnings)
add_test(NAME test_history COMMAND test_history)

add_executable(test_command_gate kernel/command/test_command_gate.cpp)
target_include_directories(test_command_gate PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_command_gate PRIVATE pixcake_command pixcake_warnings)
add_test(NAME test_command_gate COMMAND test_command_gate)
add_executable(test_photo_status kernel/command/test_photo_status.cpp)
target_include_directories(test_photo_status PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_photo_status PRIVATE pixcake_command pixcake_image_import pixcake_storage pixcake_warnings)
add_test(NAME test_photo_status COMMAND test_photo_status)

add_executable(test_project_paths kernel/project/test_project_paths.cpp)
target_include_directories(test_project_paths PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_project_paths PRIVATE pixcake_image_import pixcake_storage pixcake_warnings)
add_test(NAME test_project_paths COMMAND test_project_paths)

add_executable(test_source_presence kernel/project/test_source_presence.cpp)
target_include_directories(test_source_presence PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_source_presence PRIVATE pixcake_image_import pixcake_storage pixcake_editdoc pixcake_warnings)
add_test(NAME test_source_presence COMMAND test_source_presence)

add_executable(test_recycle_bin kernel/project/test_recycle_bin.cpp)
target_include_directories(test_recycle_bin PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_recycle_bin PRIVATE pixcake_image_import pixcake_storage pixcake_warnings)
add_test(NAME test_recycle_bin COMMAND test_recycle_bin)

add_executable(test_virtual_copy kernel/project/test_virtual_copy.cpp)
target_include_directories(test_virtual_copy PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_virtual_copy PRIVATE pixcake_image_import pixcake_storage pixcake_warnings)
add_test(NAME test_virtual_copy COMMAND test_virtual_copy)

add_executable(test_photo_marks kernel/project/test_photo_marks.cpp)
target_include_directories(test_photo_marks PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_photo_marks PRIVATE pixcake_image_import pixcake_storage pixcake_warnings)
add_test(NAME test_photo_marks COMMAND test_photo_marks)
add_executable(test_photo_query kernel/project/test_photo_query.cpp)
target_include_directories(test_photo_query PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_photo_query PRIVATE pixcake_image_import pixcake_storage pixcake_warnings)
add_test(NAME test_photo_query COMMAND test_photo_query)

add_executable(test_gpu_facade gpu/test_gpu_facade.cpp)
target_link_libraries(test_gpu_facade PRIVATE pixcake_gpu_render pixcake_gpu_facade pixcake_warnings)
add_test(NAME test_gpu_facade COMMAND test_gpu_facade)

add_executable(test_gpu_backend_qt gpu/test_gpu_backend_qt.cpp)
target_link_libraries(test_gpu_backend_qt PRIVATE pixcake_gpu_backend_qt pixcake_warnings)
add_test(NAME test_gpu_backend_qt COMMAND test_gpu_backend_qt)
set_tests_properties(test_gpu_backend_qt PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_renderd apps/test_renderd.cpp)
target_link_libraries(test_renderd PRIVATE pixcake_renderd_service pixcake_warnings)
add_test(NAME test_renderd COMMAND test_renderd)

add_executable(test_renderd_image_io apps/test_renderd_image_io.cpp)
target_link_libraries(test_renderd_image_io PRIVATE pixcake_renderd_service pixcake_warnings)
add_test(NAME test_renderd_image_io COMMAND test_renderd_image_io)

add_executable(test_exportd apps/test_exportd.cpp)
target_link_libraries(test_exportd PRIVATE pixcake_exportd_service pixcake_warnings)
add_test(NAME test_exportd COMMAND test_exportd)

add_executable(test_generated apps/test_generated.cpp)
target_include_directories(test_generated PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_generated PRIVATE pixcake_generated_service pixcake_storage pixcake_warnings)
add_test(NAME test_generated COMMAND test_generated)

add_executable(test_shell_sources apps/test_shell_sources.cpp)
target_compile_definitions(test_shell_sources PRIVATE PIXCAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_shell_sources PRIVATE pixcake_warnings)
add_test(NAME test_shell_sources COMMAND test_shell_sources)

if(APPLE)
  add_executable(test_mac_source_access platform/test_mac_source_access.cpp)
  target_include_directories(test_mac_source_access PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(test_mac_source_access PRIVATE pixcake_platform_macos pixcake_warnings)
  add_test(NAME test_mac_source_access COMMAND test_mac_source_access)
endif()

add_executable(test_exportd_image_io apps/test_exportd_image_io.cpp)
target_link_libraries(test_exportd_image_io PRIVATE pixcake_exportd_service pixcake_warnings)
add_test(NAME test_exportd_image_io COMMAND test_exportd_image_io)

add_executable(test_gpu_shader_path gpu/test_gpu_shader_path.cpp)
target_link_libraries(test_gpu_shader_path PRIVATE pixcake_gpu_render pixcake_gpu_backend_qt pixcake_warnings)
add_test(NAME test_gpu_shader_path COMMAND test_gpu_shader_path)
# macOS offscreen QPA cannot create an OpenGL context, so it would always skip
# the readback assertion.  The native platform gives real GPU evidence there;
# the test still skips explicitly when no context can be created.
if(NOT APPLE)
  set_tests_properties(test_gpu_shader_path PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_executable(test_gpu_backend_native gpu/test_gpu_backend_native.cpp)
target_link_libraries(test_gpu_backend_native PRIVATE pixcake_gpu_render pixcake_gpu_backend_native pixcake_warnings)
add_test(NAME test_gpu_backend_C57_C60 COMMAND test_gpu_backend_native)

add_test(
  NAME arch_dependencies
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/arch/check_dependencies.cmake
)

add_test(
  NAME capability_ownership
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/arch/check_capability_ownership.cmake
)

add_test(
  NAME third_party_locks_T22
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/arch/check_third_party_locks.cmake
)

add_executable(test_file_log_sink services/logging/test_file_log_sink.cpp)
target_include_directories(test_file_log_sink PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_file_log_sink PRIVATE pixcake_logging pixcake_warnings)
add_test(NAME test_file_log_sink COMMAND test_file_log_sink)

add_test(NAME test_renderd_health_writes_log
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:pixcake_app_renderd>
                 -DLOG_DIR=${CMAKE_CURRENT_BINARY_DIR}/process-logs/renderd -DPROCESS=renderd
                 -P ${CMAKE_SOURCE_DIR}/tests/apps/check_process_log.cmake)
add_test(NAME test_exportd_health_writes_log
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:pixcake_app_exportd>
                 -DLOG_DIR=${CMAKE_CURRENT_BINARY_DIR}/process-logs/exportd -DPROCESS=exportd
                 -P ${CMAKE_SOURCE_DIR}/tests/apps/check_process_log.cmake)

add_executable(test_sqlite_fast_export_latch storage/test_sqlite_fast_export_latch.cpp)
target_include_directories(test_sqlite_fast_export_latch PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_sqlite_fast_export_latch PRIVATE pixcake_storage pixcake_command pixcake_warnings)
add_test(NAME test_sqlite_fast_export_latch COMMAND test_sqlite_fast_export_latch)

add_executable(test_single_instance services/instance/test_single_instance.cpp)
target_include_directories(test_single_instance PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_single_instance PRIVATE pixcake_instance pixcake_warnings)
add_test(NAME test_single_instance COMMAND test_single_instance)
