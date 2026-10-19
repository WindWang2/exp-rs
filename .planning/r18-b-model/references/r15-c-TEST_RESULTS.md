# R15-C Test Results — 命令 / 退出码 / 环境

> 所有数字来自本机实际运行；未运行的硬件/平台测试明确标注 NOT VERIFIED。
> 环境：Windows 11，MSVC 2022 (14.38.33130) Debug，Ninja，Qt 6.8.0，GDAL 3.12.4，
> OpenCV 4.12.0（均来自共享 vcpkg 树）；构建树 `build-r15-c`（`-j2`，全局命名
> 互斥锁）；测试二进制直接运行（ctest 的 PRE_TEST discovery 未在新建树上注册，
> 沿用 R14-B 先例），环境固定：`QT_QPA_PLATFORM=offscreen`、
> `PROJ_DATA/PROJ_LIB=<vcpkg>/share/proj`（否则 CRS 断言因缺 proj.db 失败）、
> `GDAL_DATA=<vcpkg>/share/gdal`。

## 构建

| 步骤 | 命令 | 退出码 |
|---|---|---|
| configure | `.ps1_configure_r15c.ps1`（Ninja + vcpkg toolchain，共享依赖树补装 ~70 包） | 0（9108s，含 vcpkg 依赖构建） |
| 目标构建 | `.ps1_build_r15c.ps1 -Targets <25 targets> -Parallel 2` | 0 |
| `git diff --check` | `git diff --check` | 0 |

## GREEN ×2（修复在位）

| 目标 | 结果 | 断言数 / 用例数 |
|---|---|---|
| test_r15_C_manifest_contracts（C02） | PASS | 18 / 6 |
| test_r15_C_publish_recovery（C13 F1/F2） | PASS | 46 / 3 |
| test_r15_C_chunk_admission（C11/C12） | PASS | 34 / 5 |
| test_r15_C_detection_decode（C08） | PASS | 12 / 5 |
| test_r15_C_provider_degradation（C05/C16） | PASS | 29 / 3 |
| test_r15_C_runtime_contracts（C07） | PASS | 17 / 2 |
| test_model_tasks（含 B5） | PASS | 12816 / 20 |
| test_model_ensemble（含 C09） | PASS | 120 / 15 |
| test_eo_platform_10（含 B10 翻转 + fake 声明修复） | PASS | 578 / 21 |
| test_model_runtime（回归） | PASS | 100394 / 21 |
| test_model_failure_matrix（回归，含 #1455 ledger） | PASS | 2158 / 19 |
| test_multimodal_inference（回归） | PASS | 8324 / 8 |
| test_r14_B_tile_engine（回归） | PASS | 20695 / 4 |
| test_r14_B_detection_decode（回归） | PASS | 279 / 7 |
| test_chunk_adoption_11（回归，含 Refuse 梯级） | PASS | 435 / 14 |
| test_chunk_graph（回归，含 double-run） | PASS | 365 / 31 |
| test_model_runtime_stress（回归） | PASS | 68 / 6 |
| test_tensor_blob（回归） | PASS | 173 / 7 |
| test_r14_B_runtime_providers（回归） | PASS | 15 / 4 |
| test_model_catalog_v2（回归） | PASS | 418 / 24 |
| test_model_manifest7（回归） | PASS | 66 / 15 |
| test_model_manifest_r4（回归） | PASS | 35 / 14 |
| test_model_library_manifests（回归，26 个 manifest 全过） | PASS | 237 / 6 |
| test_gpu_plane（回归） | PASS | 35 / 5 |

GREEN 运行两次（修复恢复后完整重跑一次，构建一次）：结果一致。

## Review 第 1 轮修复后的复验（GREEN 第 3 次）

24 个目标完整重跑，全部通过（新增/变更：test_r15_C_publish_recovery 62 assertions/4 cases、
test_r15_C_provider_degradation 36/4；其余不变）。

P1 修复（VRAM→ResourceBudgetExceeded）单独 RED 验证：`git stash` 仅
`src/operators/runtime/model_execution_service.cpp` → 重建 → 该用例断言失败
（exit 42，`e.code()==ResourceBudgetExceeded` 失败）→ 恢复 → GREEN。

## RED（stash 全部 src 修复，仅保留测试；master 等价代码）

| 目标 | 结果（预期 RED） |
|---|---|
| test_r15_C_manifest_contracts | 3/6 用例失败（C02 三项检查缺失）✓ |
| test_r15_C_publish_recovery | 2/3 失败（F1/F2 后缀被删除）✓ |
| test_r15_C_chunk_admission | 4/5 失败（census 超预算 + ledger 3 项）✓ |
| test_r15_C_detection_decode | 1/5 失败（远距框 dedup 键回绕）✓ |
| test_r15_C_provider_degradation | 3/3 失败（B2/B4/B3）✓ |
| test_r15_C_runtime_contracts | 1/2 失败（min_valid_coverage 被忽略）✓ |
| test_model_tasks | 1/20 失败（conf/nms_iou 无范围校验）✓ |
| test_model_ensemble | **通过** — C09 缺陷被 GDAL Float32→Byte 钳制掩盖（见下） |

**C09 的诚实记录**：master 上该用例通过，因为 GDAL 3.12 的 Float32→Byte 转换把
越界值 64770 钳制回 255，恰好等于 sentinel。缺陷代码路径（死 isnan 分支 + 越界
量化值写出）经代码推演确证；可观测损坏取决于 GDAL 转换语义（钳制则掩盖，回绕则
把 NoData 变成貌似合理的 agreement 值 2/1066）。测试仍锁定正确的 sentinel 契约。

## 已知环境限制（NOT VERIFIED）

- **test_device_planner 在本宿主挂起**：已用主 checkout 的 master 构建二进制复现
  同样挂起 → 预先存在的环境问题（无 GPU/NVML 的主机），**非本轨回归**；其覆盖的
  VramLedger 单元契约由本轨 test_r15_C_chunk_admission 的三个用例直接覆盖。
- OpenVINO / TensorRT / ONNX Runtime 执行车道：本宿主无依赖与硬件 → 相关修复为
  代码级（compile-only），未编译未运行；不伪造 GREEN。
- CUDA 路径：全部未验证。
- Linux：未验证。

## C15 性能测量

见 `PERFORMANCE.md`（test_model_runtime_bench，SICNU_MODEL_BENCH=1，本轨 vs master
同宿主对比：持平或略优，无回归）。
