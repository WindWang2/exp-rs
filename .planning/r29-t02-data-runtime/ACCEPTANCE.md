# ACCEPTANCE — R29-T02（对照 B 表，验证后填实测）

| 编号 | 要求 | 状态 | 证据 |
|---|---|---|---|
| B1 | ... | ✅ | test_r29_t02_published_claim（H-A~E）+ WP01 脚本（CLI 全链） + R8 框架测试 |
| B2 | ... | ✅ | 身份=canonicalParams（含 outputPath+inputBytes+inputMtime）；R7 锁既有；test_chunk_resume_11 回归；链③失败不污染 |
| B3 | ... | ✅ | test_r29_t02_virtual_warp（preflight 分支 + warp extent/pixel/NoData oracle + 北向/旋转）+ test_virtual_raster_preflight/create 回归 + 链② |
| B4 | ... | ✅ | 链②（cancel→restart resume→VRT→recipe reopen）+ test_r28_t02_import_session_runtime 回归 |
| B5 | ... | ✅ | test_cartography_production_11 SICNU_CP11_THREAD_PARITY=1 + 链③（真实 ink + 失败隔离 + 再成功） |
| B6 | ... | ✅（PR 提交后补 URL） | WP01 脚本（链①）、test_r29_t02_chains（②③）、回归清单见下 |

## 回归清单（构建完成后运行）

test_chunk_resume_11, test_chunk_adoption_11, test_chunk_contract_11,
test_runtime_chunk_boundaries_r4, test_r27_t02_resume_lock,
test_chunked_operators_parity, test_chunked_fabric_e2e, test_output_committer,
test_io_atomic_failures, test_workflow_cache_e2e（#667 自愈）,
test_r28_t02_import_session_runtime, test_r28_t02_closed_loop,
test_r28_t02_task_lifecycle_batch, test_r28_t02_geospatial_interop,
test_virtual_raster_preflight, test_virtual_raster_create,
test_swarm_fix_data（DAT-2 XML 字节等）, test_cartography_production_11（含 parity=1）,
test_task_center*, test_job_engine*, test_execution_plane_*（按 ctest -N 实名筛选）

## 资源证据

- 全程双锁（host-build + build-dev）-j4；被 T03 轨排队时等待（--wait 3500）不抢锁。
- ICE/坏 so 事件与处置记录在 LEDGER 第 3 轮与 RESUME3。

## 实测结果（2026-11-11，绑定提交前 HEAD）

- B1/B2：test_r29_t02_published_claim 73✅ + WP01 16/16✅（含 H-C 删除自愈、H-D 内容漂移、H-E 跨工作区、R8 demote 后 R2 复用 4/4）
- B3：test_r29_t02_virtual_warp 144✅（preflight 判定 + 真实 warp extent/像元/NoData oracle，独立 OGR 变换）+ virtual_raster_preflight/create/swarm_fix_data 回归✅
- B4：链② 100 断言内（cancel→restart resume 不双注册、稳定资产 ID、真实 4326→32648 warp、recipe 重开一致）+ r28_t02_import_session_runtime✅
- B5：cp11 SICNU_CP11_THREAD_PARITY=1 24✅ + 链③（真实 ink、失败不污染、再成功）
- B6：回归 68/68 全绿（55+35）；R1 独立审查 14 项全处置；R2 复核见 REVIEW_R2.md
