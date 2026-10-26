# R21-B TEST_RESULTS — 执行证据（2026-10-24）

## 环境
- worktree exp-rs-r21-B-20261023T185101Z；GCC 16.2.1；CMake 4.4；Release；SICNU_EMBED_PYTHON=ON（python 族需要）
- 依赖：vendor prefix（GDAL/PROJ/GEOS/GSL/NetCDF）+ 系统共享 zlib/zstd/expat/sqlite3（vendored 静态库 non-PIC 断裂）+ link-shim（r19-02 配方：pcre2 10.47 + --allow-shlib-undefined）— 断裂根因见 CROSS_TRACK_FINDINGS X1
- 测试运行：QT_QPA_PLATFORM=offscreen，PROJ_LIB=vendor/usr/share/proj，LD_LIBRARY_PATH 含 link-shim + vendor lib

## RED（pristine src=911e0b17 + 新测试，实跑）
| 目标 | 输出 |
|---|---|
| test_artifact_store | 13 cases/1 failed; 134 断言/133 过 — `stamped=="0"` 失败（INSERT OR REPLACE 覆写） |
| test_data_source_providers | 14 cases/1 failed; 127/122 — 5 断言（canonicalSource 拼法 + dedup） |
| test_dataset_core | 19 cases/1 failed; 512/508 — `conflicts==40` 得 0；wrongCodes=40 |
| test_experiment_consistency_r4 | 11 cases/1 failed; 298/296 — run_metrics 残留非空（孤儿行） |
| test_model_runtime | 26 cases/1 failed（新用例）— embMax 0.0==0.37（二值化） |
| test_python_engine | 7 cases/1 failed — evalString false（AttributeError） |
| test_model_runtime_9 | 新 2 用例 — pristine 全过（保护性；fixture 半开坐标修正后为正确 oracle） |
| test_r21_b_end_to_end | pristine 全过 4142 断言（保护性） |

## GREEN（修复后，两遍）
| 目标 | pass1 | pass2 |
|---|---|---|
| test_artifact_store | All tests passed (134/13) | All tests passed (134/13) |
| test_data_source_providers | All tests passed (127/14) | — |
| test_dataset_core | All tests passed (512/19) | — |
| test_experiment_consistency_r4 | All tests passed (298/11) | — |
| test_model_runtime | All tests passed (110440/26) | All tests passed (110440/26) |
| test_model_runtime_9 | All tests passed (1263/19) | — |
| test_python_engine | All tests passed (45/7) | — |
| test_r21_b_end_to_end | All tests passed (4142/2) | — |

## M1 R20-02 成果复验（O-B1）
| 目标 | 结果 |
|---|---|
| test_model_runtime（F1/F2/F4 + R2-1） | All tests passed (110440 断言/26 cases) |
| test_model_catalog_v2（F3） | All tests passed (436/24) |
| test_workspace_services（F5） | All tests passed (212/17) |
| test_fault_injection（F6） | All tests passed (162/9) |

## 域族 sweep（19/19 PASS）
chunk_graph(371/34)、data_governance_consistency(28/2)、dataset_fingerprint_determinism(68/6)、experiment_evaluation(586/34)、gpu_plane(35/5)、model_manifest7(66/15)、model_runtime_8(2404/19)、governance_store(236/16)、asset_catalog_index(317/8)、workspace_catalog(804/7)、dataset_version_migration(33/4)、workspace_stress(20/1)、store_fault_injection(58/4)、runtime_pool_recovery_r5(214/14)、r15_C_chunk_admission(34/5)、model_library_manifests(237/6)、model_runtime_stress(68/6)、experiment_txn_r4(42/3)、dataset_e2e_examples(58/4)

## 未执行（诚实声明）
- GPU lane：无 GPU 环境 → NOT_EXECUTED
- TSan：宿主无配方 → NOT_EXECUTED
- Windows/macOS：无环境 → NOT_EXECUTED
- 未做完整全树构建（对象库依赖已全编译；38 域族 sweep 由已构建目标组成）
