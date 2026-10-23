# R20-02 TEST_RESULTS

## 环境（实测）
- 主机: cachyos-x8664, Linux 6.x, 40 核 / 62 GB；本轨全程全局锁 `-j2`（flock `$GIT_COMMON_DIR/exp-rs-zcode-global-build.lock`）
- 编译器: GCC 16.2.1（构建期 ICE ×4 由 make 重试自愈；ld 段错误 ×1 重跑恢复——均为工具链瞬时故障，产物验证无恙）
- 构建: CMake 4.4.4 / Unix Makefiles / Debug / ENABLE_TESTS=ON / ENABLE_LOCAL_BUILD_SHORTCUTS=ON；系统 GDAL/PROJ/GEOS（/usr/lib/cmake/*，与主仓 build-dev 同配方）
- 测试: QT_QPA_PLATFORM=offscreen 直跑 Catch2 二进制（本机有 ctest 亦可）

## RED 证据（pristine master 28e1891390 引擎 + 新测试；2026-10-23 04:1x-04:2x 实跑）
| 测试二进制 | 命令 | 结果 | 失败明细 |
|---|---|---|---|
| test_model_runtime | `QT_QPA_PLATFORM=offscreen ./test_model_runtime`（600s timeout） | 24 用例 3 failed / **100,410 断言 100,407 过** | F1: `InvalidInputData: mixed tile sizes in one inference batch (36x64 vs 64x64)`；F2: `CHECK( mismatchedConfidence == 0 )` 扩张值 `2048 == 0`；F4: `REQUIRE_THROWS_AS ... because no exception was thrown` |
| test_model_catalog_v2 | 同上 | 24 用例 1 failed / 436 断言 433 过 | F3×3: bad-interpolation / bad-interpolation-case / bad-resize-token `readiness == Ready(1) != InvalidManifest(2)` |
| test_workspace_services | 同上 | 17 用例 1 failed / 212 断言 209 过 | F5×3: ok 仍 true、error 无 "governance"、snap-*.snapshot 目录未清理 |
| test_fault_injection | 同上 | 9 用例 1 failed / 157 断言 156 过 | F6: `REQUIRE( second )` — put 返回 nullopt |

结论：6/6 RED 全部命中预期根因；四个二进制合计 101,215 断言中仅 13 条失败且全部是本轨新种子 → **无预存红、无回归混杂**。

## GREEN（修复 57b15509fd 状态；2026-10-23 07:0x-07:1x 实跑，QT_QPA_PLATFORM=offscreen 直跑）
| 二进制 | pass1 | pass2 |
|---|---|---|
| test_model_runtime | **All tests passed (110,416 assertions in 24 cases)** | All tests passed (110,416) |
| test_model_catalog_v2 | All tests passed (436 in 24) | All tests passed (436) |
| test_workspace_services | All tests passed (212 in 17) | All tests passed (212) |
| test_fault_injection | All tests passed (162 in 9) | All tests passed (162) |

（test_model_runtime 断言数从 RED 期 100,410 → 110,416：F1 修复后边缘 tile 用例真正跑完推理与逐点断言。）

## R2 驱动复验（R2-1 probe 路径；修复前 stash 取 RED → 恢复后重建）
- **RED**（57b15509fd 引擎 + 新测试）：`InvalidInputData: mixed tile sizes in one inference batch (36x64 vs 64x64)` — probe 把 36 宽 deferred tile 压进未 flush 的 64 宽 pending 批，机制与 R2 推导一致（实跑输出留档）。
- GREEN（R2 修复后）：待 build5 完成回填。

## 宽回归 sweep（修复 57b15509fd 状态，38 个域族二进制串行 -j1）
**PASS=38 / FAIL=0 / SKIP=0**：chunk_graph、d19_dataset_foundry、data_governance_consistency、dataset_{core,e2e_examples,fingerprint_determinism,quality_scale,version_migration}、experiment_{benchmark_r4,bridge_r4,capsule,consistency_r4,debugger,evaluation,parity_r4,txn_r4}、fault_injection、governance_store、gpu_plane、model_{ensemble,failure_matrix,library_manifests,manifest7,runtime,runtime_8,runtime_9,tasks,catalog_v2}、artifact_store、asset_catalog_index、workspace_{catalog,services,snapshot,state,stress}、store_fault_injection、runtime_pool_recovery_r5、r15_C_chunk_admission。
另：test_rs_operator 832 断言、test_io_paths 67、test_io_products 46、test_io_remote_validator 232、test_io_uri 211、test_io_range_cache 314 全绿。

## 合并存续验证（R19-01 修复的运行时回归）
- zarr.json 两段扩展名（#1637-2）：test_io_paths 全绿（含 two-segment 用例）
- 4MiB 有界读（#1637-3）：test_io_products 全绿
- setRange 运行时强制（#1578）：test_rs_operator 832 断言全绿
- SSRF 标志传播（#1545）：test_io_remote_validator 232 + test_io_range_cache 314 全绿
- worker 路由设门（R19-01 R2）：model_runtime 家族全绿
- chunk addSink 守卫（#1553）：test_chunk_graph 全绿
- GPU 析构锁（#1554）：test_gpu_plane 全绿
- dataset/experiment 全族（#1580-#1586 修复）：sweep 列表全绿
