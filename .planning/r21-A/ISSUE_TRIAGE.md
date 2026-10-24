# ISSUE_TRIAGE — r21-A

勘查时间：2026-10-24 LIVE（126 条 open 全量，`gh issue list --state open --limit 200`）。
分级口径：OPEN-UNFIXED（master 现码无修复）/ FIXED-OPEN（已修未关，本文件给实读证据）/ PARTIAL / OBSOLETE / DUPLICATE / NEEDS-EVIDENCE / OUT-OF-SCOPE。

## A. 本轨领域（src/analysis、src/processing/algorithms、src/operators/rs）

| # | 级 | 位置 | LIVE 结论（HEAD=911e0b17） | 证据 | 处置 |
|---|---|---|---|---|---|
| 1550 | P1 | rs_jm_separability.cpp | **FIXED-OPEN**（对数域 logDeterminant + rank-deficient -1.0） | :48/:61-69/:149-161 注释引 #1550 | 不重修；回归两遍绿 + 建议 close |
| 1538 | P1 | image_enhancement_streaming.cpp:984 | **FIXED-OPEN**（`!isfinite‖==nodata` 建图门） | :984-989 注释引 #1538 | 同上 |
| 1590 | P2 | rs_post_process.cpp:122-128 | **FIXED-OPEN**（border 投票 0 不计） | 实读 + modeOfWindow 注释 | 同上 |
| 1591 | P2 | rs_segment_map.h:98-102 | **OPEN-UNFIXED（潜在竞态）**：mutable 缓存 const 惰性构建无同步；R20-01 记档未修 | 实读头文件 | **R21-A 修复候选 #1**（本轨所有权内） |
| 1631 | P3×3 | cross_validation/kmeans/feature_scaler | **FIXED-OPEN（3/3）** | Fisher-Yates :55-59 / y 守卫 ADR0061 块 / fromJson fail-closed :166-172 | 不重修；回归两遍绿 |
| 1646-6 | P3 | nodata_utils.h 所有权 | **FIXED-OPEN** | consumers grep 全覆盖（resampler/spectral_*/segment_*/multires/sar_calibration） | 不重修 |
| 1467 部分 | P1/P2 | rs_spatial_cross_validation.h 双实现 vs src/core/spatial_split.cpp | **OUT-OF-SCOPE（跨轨）**：spatial_split.cpp 在 src/core（B 轨禁区） | R20-01 CT-3 | CROSS_TRACK 登记 |
| 1544 | P1 | rs_continuum_removal / rs_spectral_resample | **FIXED-OPEN**（DatasetFileGuard RAII；R20-01 实读） | 本轮抽样复核：见 TEST_RESULTS | 不重修；回归覆盖 |

## B. 跨轨（登记 CROSS_TRACK_FINDINGS，本轨不修）

- **#1541** detection_tile_engine CV_CN_MAX — R20-01 已确认 master 修复（:331），建议 02 轨 close。
- **#1642-1/2/3** operators framework/runtime/io — 02/03 轨。
- **#1646-1..5** processing framework（GDALAllRegister 绕过 ensureGdalInit、local_worker_pool 无锁读、gdal move 丢 lastError、json_params 重复声明、help catalog 14 死键）— 02/03 轨。注：#1646-1 的 algorithm_preflight.cpp 在 `src/processing/framework`（共享灰区），非本轨算法内核，仍归框架轨。
- **#1573/1575/1576/1577** operators runtime TensorRT/OpenVINO/setRange — 02 轨。
- **#1467 主体**（temporal_cube 静默 NaN、radiometric_state GDALOpenEx、qgsdatasourceresolver、sicnu_plugin_interface QWidget）— src/core → 02 轨。
- **#1565–#1569** src/processing providers/框架 — 02/03 轨（非本轨算法内核）。
- **#1558** tests/test_e2e_open_issues.cpp 恒真断言 — 测试健康域（test_e2e_open_issues 非本轨算法测试文件）→ 05/E 轨；本轨 CROSS_TRACK 登记。
- **#1491** CI 无完成结果 — 基建，非本轨。
- 其余 P2/P3（app/gui/agent/cli/workflow/verify/teaching/preflight/dataset/experiment/geospatial/faultlab/python/native/help/explain 等，#1551–#1658 主体）— 对应 02/03/04/05 轨。

## C. R20-01 记档未修 → R21-A backlog（入 TASKS）

1. #1591 RsSegmentMap 竞态（上表）。
2. image_fusion.cpp 内存核：均值样本集 vs 协方差样本集不一致 + ±Inf 门只测 isnan（流式正确）。
3. fast_6s_lookup.cpp:115-116 水汽 0.5 系数（无权威 oracle → 本轮只做可证部分或明确 NOT-FIXABLE-WITHOUT-ORACLE）。
4. rs_simple_segmenter 31-bit 哈希碰撞（统计量化后再定）。

## D. 动态说明

其他 Agent 可能并行建 PR；每次提交前重新 `git fetch origin` + `gh pr list --state open` 对账（见 LOOP_LEDGER）。
