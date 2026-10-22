# BASELINE — zcode-r20-01 (科学算法 / 处理链 / 数值·性能收敛)

## LIVE RECON（2026-10-22 UTC，全部只读）

- 勘查目录：`/home/kevin/projects/rs-studio/main`（root master，未修改）
- `git fetch --prune origin` 成功；**origin/master = `28e1891390bb812dc7ef2fe045ac90492c9525f1`**（与任务快照一致，无新提交）
- 远程分支：仅 `master`（R18/R19 agent 分支已全部删除）；本地存在大量历史 worktree（flash-*/rs14-*/hardening-*），全部不属于本轨，不触碰
- PR 状态（gh，--limit 30）：最新 #1667 (r19-01)、#1666 (r19-02)、#1665 (r19-03)、#1664 (r18-e)、#1663 (r18-a) 全部 MERGED；**无 open PR**
- 开放 issue：126 条（P1×24、P2×66、P3×34、无标签×2）
- gh 查询全部成功，无查询失败项

## Worktree

- 分支：`agent/r20-01-scientific-algorithms-20261022T160501Z`
- 路径：`/home/kevin/projects/rs-studio/exp-rs-agent/r20-01-scientific-algorithms-20261022T160501Z`
- BASE = origin/master = `28e1891390bb812dc7ef2fe045ac90492c9525f1`

## 快照候选 issue 的 LIVE 复核（关键判断）

| Issue | 快照判断 | LIVE 现码结论 | 证据 |
|---|---|---|---|
| #1590 sieve 计 0 票 | 疑似已修未关 | **已修**（`rs_post_process.cpp:129` `if (vote == 0) continue`，注释引用 modeOfWindow 契约） | grep 实读 |
| #1550 JM 上溢 | 待查 | **已修**（`rs_jm_separability.cpp` logDeterminant 对数域计算 + rank-deficient -1.0 n/a 分支） | sed 实读 |
| #1538 HistEqualize ±Inf | 待查 | **已修**（`image_enhancement_streaming.cpp:980` `!std::isfinite(v) || v == nodata`，注释直接引用 #1538） | sed 实读 |
| #1544 GDAL 句柄取消泄漏 | 待查 | **已修**（continuum_removal + spectral_resample 均有 `DatasetFileGuard outGuard{...}`） | grep 实读 |
| #1541 CV_CN_MAX | 待查 | **未验证**（`src/operators/runtime/detection_tile_engine.cpp` 属 02 轨禁区，登记 CROSS_TRACK） | — |

结论：四个候选在 master 已修复；本轨按协议只补「保护性回归验证」或在缺测试时补测试，不重复修。需逐一确认现有测试是否已覆盖（test_post_process / test_jm_separability / test_histogram_equalize_edge_cases / test_continuum_removal 等存在）。

## 本轨领域开放 issue 初筛（126 条中）

**本轨允许区（src/analysis、src/processing/algorithms、src/operators/rs、绑定算法的 providers）：**
- #1631 [P3×3] src/analysis/classification：① rs_cross_validation std::shuffle 实现定义排列 vs 姊妹文件 Fisher-Yates（可复现契约漂移）；② rs_classifier_kmeans/isodata 训练前未校验 y 类型/CV_32S/rows 一致（类型双读+空 data 解引用路径）；③ rs_feature_scaler fromJson 不校验 min/max 数组长度（静默回退 0/1 → 数据漂移）
- #1646 第 6 项 [P3] nodata_utils.h 声称唯一所有权但仅 1 处使用；resampler/spectral_anomaly/spectral_local_rx 各自重实现且强弱不一（A02 数值契约核心）
- #1591 [P2] RsSegmentMap mutable 缓存 const 方法惰性构建无同步（潜在竞态，未发现跨线程调用方）
- #1467 部分：rs_spatial_cross_validation.h 与 src/core/spatial_split.cpp 的 buffered block folds 双实现（前者在 analysis，后者在 core 跨轨）

**跨轨（登记 CROSS_TRACK_FINDINGS，不修）：**
- #1541 detection_tile_engine CV_CN_MAX（02 轨 operators/runtime）
- #1642 四项（operators/framework registry 锁内调工厂、operators/runtime const_cast、operators/io schema string-array）
- #1646 其余（processing/framework GDALAllRegister 绕过 ensureGdalInit、local_worker_pool 无锁读、gdal_dataset_wrapper move 丢 lastError、json_params_converter 重复声明、algorithm_help_catalog 14 死键）
- #1577/#1575/#1576/#1573 operators/runtime TensorRT/OpenVINO（02 轨）
- #1467 主体（src/core temporal_cube/radiometric_state 等）

## 构建计划

- BUILD_DIR=`build-r20-01`，`-DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON`
- 全局锁：`$(git-common-dir)/exp-rs-zcode-global-build.lock`，默认 `-j2`
- 测试 `QT_QPA_PLATFORM=offscreen ctest -j1`
