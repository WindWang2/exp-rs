# R25-B TEST_RESULTS

## RED 证据（BASE=2610cb4741，2026-10-28 采集）

方法：BASE detached worktree + HEAD 测试 TU + BASE 源码 include 路径，`-fsyntax-only`，生产编译旗（取自同库现有 TU 的 compile_commands）。

| 测试 TU | BASE 编译退出码 | 首个错误 |
|---|---|---|
| test_r25_b_import_preflight.cpp | **1** | `data/import_preflight.h：没有那个文件或目录` |
| test_r25_b_inference_plan.cpp | **1** | `operators/runtime/inference_plan.h：没有那个文件或目录` |
| test_r25_b_replay_deviation_v2.cpp | **1** | `‘ReplayDeviationEntry’不是一个类型名` + `no member named ‘deviationDetails’` |

HEAD（本轨分支）同旗语法检查：6 个 TU（3 生产 + 3 测试）+ E2E TU 全部 **exit 0**。

## GREEN（实跑，2026-10-28）

- **`ctest -R "r25_b"`（R2 修复后）第一轮**：**21/21 通过**
- **第二轮**：**21/21 通过**（两轮一致）
- **R2 修复前**：17/17 通过（R1 收口后）；R2 修复过程中曾 19/21、20/21 暴露测试自身与契约理解偏差，逐项修正后 21/21（见 REVIEW_R2.md「测试修正」如实记录）（1 用例按 netCDF 驱动可用性诚实 SKIP：`test_r25_b_import_preflight::...container subdatasets`——本 GDAL 无 netCDF 创建支持；SKIP 非 PASS 伪装）。
- **R1 收口后第二轮**：**17/17 通过**（同上 SKIP）。双轮一致，排除侥幸。
- 其中：test_r25_b_import_preflight 12 用例（含安全泄露守卫、qint64 精确性、SAFE 目录产品）、test_r25_b_inference_plan 6 用例（含 R1+R2 收口两大家族）、test_r25_b_replay_deviation_v2 2 用例、test_r25_b_end_to_end 1 用例。
- R1 修复后新增 6 个 SECTION（dtype/roles/mode-conflict/conf-bounds/scene-lane/multi-input 通道核算）全绿；其中 multi-input SECTION 首版误用单输入 temporal 契约（被 master 的 rejectUnwiredContracts 正确拒绝），按 run seam 合法形态（每契约一 feed）修正后 GREEN。
- 构建：`flock ~/.cache/exp-rs-build.lock cmake --build build-r25-B --parallel 2 --target <本轨+邻域目标>` **exit 0**（shared host，兄弟轨 R25-A/R25-C 持锁排队串行；-j2 上限遵守）。
- **注册完整性门**：`python3 scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs` → `1067 registered targets + 50 add_test names at HEAD (base 1063/50; lost: 0+0; added: 4+0)`，`0 marker finding(s); 0 duplicate(s); mode=git`，**exit 0**。
- **`git diff --check`**：干净。

## 邻域回归

命令：`ctest -R "r24_b_|r23_b_|test_data_manager|test_dataset_core|test_experiment_consistency_r4|test_model_runtime$|test_mlops9_evidence|test_collection_import_service" -j1 --output-on-failure`

| 轮次 | 结果 |
|---|---|
| 首轮 | 39/45 ran；6 个 NOT_BUILT（构建目标未纳入首次闭包）→ 补齐目标 |
| 补齐后 | **40/40 通过，0 failed，0 NOT_BUILT**（含 r24_b 全 8 套件、r23_b 2、mlops9、experiment_consistency_r4、dataset_core、model_runtime、collection_import、data_manager 全族） |

## 构建环境备注

共享宿主：R25-A/R25-C 并行构建持锁排队；`CMAKE_BUILD_PARALLEL_LEVEL=2`、flock 单锁、ctest -j1、QT_QPA_PLATFORM=offscreen。墙钟性能见 PERF_BASELINE.md（仅报告，不设硬门槛）。
