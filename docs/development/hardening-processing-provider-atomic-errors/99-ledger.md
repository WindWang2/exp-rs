# goal-loop round log — hardening/processing-provider-atomic-errors

(Track ledger kept here; the tracked `.goal-loop-ledger.md` at repo root belongs
to other tracks and is left untouched.)

Round 1 | Phase 0 recon + 深读：authority graph (atomic_fs / OutputCommitter / adapters / task_center) + 五族算法输出完整性矩阵；确认 F1 (close flush 吞掉 ×12 处)、F2 (直写最终路径毁旧结果)、F3 (enhancement 无 partial 清理)；历史分支 5 commit 逐条核对（2 已修、3 部分残留） | `00-recon.md` 落盘 | PASS | slice 实现

Round 2 | Slice 1a+1b 实现：fault point `gdal_wrapper.close_flush`（closeWithError / closeDatasetFailClosed 注入真实 CPLError）；closeWithError 采用（atmospheric ×3、fusion ×6 分支、enhancement ×2、calibration、satellite）+ closeDatasetFailClosed（satellite、raster_calculator、raster_ndvi、raster_merge_bands）；StagedRasterOutput（atomic_fs 组合，无新语义）staged 原子发布接入 16 个直写入口；oracles 进 test_fault_matrix（注入 close 失败 → 两变体返回 false；QUAC flush 失败 → fail truthfully + 旧结果字节不变 + 无 .tmp 残留 + 解除 arm 后恢复成功） | test_fault_matrix 82/8 全绿 ×2；变异注入（回退 QUAC close 门）→ oracle RED；恢复 → 绿 | PASS | slice 2

Round 3 | Slice 2 实现（recon agent 证据驱动）：otb_pixel_info / otb_read_image_info fail-closed（fail-open `return {}` ×5 → throw；watchdog；terminate→kill 阶梯；CrashExit 分类）；gdaltransform CrashExit 检查 + 取消 throw（原 `return {}` 空成功）+ OUTPUT 写入/flush 检查；provider_algorithm_adapter catch 块 typed `Cancelled` 分类恢复；gdal_tool_wrapper stdout-dump 短写检查；feature_cube writeSidecar 全量写 + flush；gcp_manager saveToCsv flush/status 检查；oracles：tests/test_processing_error_paths.cpp（fake-tool: kill -9 → crashed；cancel → throw；exit 1 → exit-code+stderr；healthy 反向守护 ×3；/dev/full 短写 → false = 旧实现 RED 证据） | test_processing_error_paths 46/6 全绿 ×2（含 /dev/full RED 设计）；回归 5 suite 全绿（103+34+2454+52+76 断言） | PASS | review→PR
