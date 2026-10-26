# R21-B RED_GREEN 执行计划与证据

## 阶段
- A 基线全量构建（进行中，pristine 911e0b17 源 + 后半混合；产物只作对象缓存种子，不作证据）
- B RED：`git checkout 911e0b17 -- src/`（仅生产码回退，保留测试）→ 增量编译 7 个受影响测试目标 → 实跑断言失败即 RED 实锤
- C GREEN：`git checkout HEAD -- src/` → 增量编译 → 两遍实跑
- D M1 复验：R20 F1-F6 族（model_runtime / model_catalog_v2 / workspace_services / fault_injection）两遍绿 + 38 域族 sweep

## 受影响测试目标
| 目标 | 新用例 | 预期 RED 点 |
|---|---|---|
| test_data_source_providers | missing-source symlink identity | canonicalSource 含 link 拼法 ≠ real；二次注册 2 资产 |
| test_dataset_core | concurrent create races | 败者 store_write_failed ≠ conflict |
| test_experiment_consistency_r4 | refused run install no orphan metric | listMetricRecords 1 行残留 |
| test_artifact_store | stamp never rewritten | '0' 被覆写为 '1' |
| test_model_runtime | mask_threshold non-class heads | embedding 带全 0（二值化） |
| test_python_engine | sicnu.project_path real API | AttributeError |
| test_model_runtime_9 | R2-4 两用例 | 保护性（无 RED，验证既有正确行为） |
| test_r21_b_end_to_end | 全链 + 失败腿 | 新增链路（无 RED；由组成件回归保护） |

## RED 实锤证据（2026-10-24，pristine src=911e0b17 + 新测试，实跑输出）
| 目标 | 结果 | 失败断言 |
|---|---|---|
| test_artifact_store | 13 cases/1 failed, 134 断言 133 过 | `stamped == "0"`（'0' 被 INSERT OR REPLACE 覆写为 '1'）|
| test_data_source_providers | 14 cases/1 failed, 127 断言 122 过 | 5 条：canonicalSource 拼法 + dedup + reusedExisting + assets().size()==1 |
| test_dataset_core | 19 cases/1 failed, 512 断言 508 过 | `conflicts==40` 得 0（败者 store_write_failed）+ wrongCodes=40 |
| test_experiment_consistency_r4 | 3 cases/1 failed, 105 断言 103 过 | run_metrics 残留非空（孤儿行）|
| 环境 | PROJ_LIB=vendor/usr/share/proj；LD_LIBRARY_PATH 含 link-shim（pcre2 10.47）| — |

## 追加 RED 实锤（批次 2/3，2026-10-24）
| 目标 | 用例 | RED 输出 |
|---|---|---|
| test_model_runtime | mask_threshold leaves non-class heads continuous | embMax `0.0f == 0.37`（embedding 被二值化） |
| test_model_runtime_9 | blend/nodata | `nanCount==32` 得 **15**（blend lane NoData 恢复漏一半） |
| test_model_runtime_9 | multimodal/nodata | `s1Nan==s1Total` 得 **93/128**（双 feed 无效区未全恢复） |
| test_python_engine | sicnu.project_path real API | evalString false（pristine AttributeError） |
| test_r21_b_end_to_end | 全链 | **pristine 即 PASS**（4142 断言，2 cases）→ 保护性 fixture 非 RED |

## D1/D2：R20-02 R2-4 跟进项的真实缺陷（非测试缺口）
- D1：单输入 feather-blend lane 对核心内 NoData 像素恢复不完整（15/32）
- D2：多输入 lane every-feed-invalid 恢复不完整（93/128）
→ 列入 M2 修复波次（G8），要求 RED→GREEN×2
