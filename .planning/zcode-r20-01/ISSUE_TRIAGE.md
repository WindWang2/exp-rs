# ISSUE_TRIAGE — zcode-r20-01

勘查时间：2026-10-22（LIVE，`gh issue list --state open --limit 500`，126 条全量）。逐条结论如下；「已修未关」= master 现码已含修复（本文件给出实读证据），不重复修，建议关闭或补保护性回归。

## A. 本轨领域 issue（src/analysis、src/processing/algorithms、src/operators/rs、算法绑定 providers）

| # | 严重度 | 位置 | LIVE 结论 | 证据 | 处置 |
|---|---|---|---|---|---|
| 1550 | P1 | rs_jm_separability.cpp | **已修未关**：logDeterminant 对数域 + rank-deficient -1.0 分支，注释直接引用 #1550 | sed 实读 135-175 行 | 建议关闭；test_jm_separability.cpp 已存在 |
| 1538 | P1 | image_enhancement_streaming.cpp:980 | **已修未关**：`!std::isfinite(v) ‖ v==nodata`，注释引用 #1538 | sed 实读 | 建议关闭 |
| 1544 | P1 | rs_continuum_removal / rs_spectral_resample | **已修未关**：两处 `DatasetFileGuard` RAII | grep 实读 | 建议关闭；test_continuum_removal.cpp 存在 |
| 1590 | P2 | rs_post_process.cpp:129 | **已修未关**：`if (vote == 0) continue` + 注释引用 modeOfWindow | grep 实读 | 建议关闭 |
| 1591 | P2 | rs_segment_map.h:98-102 | **仍开放（潜在）**：mutable 缓存无同步，issue 自述"未发现跨线程调用方" | 实读 | 本轮不改行为（无活跃竞态调用方）；记入 CROSS_TRACK/后续提案 |
| 1631 | P3×3 | rs_cross_validation / rs_classifier_kmeans / rs_feature_scaler | **已修未关（3/3）**：Fisher-Yates 已替换 shuffle（注释自证）、KMeans y 守卫已存在、fromJson min/max 长度校验 fail-closed 已存在 | 三处实读 | 建议关闭；test_cross_validation/test_feature_scaler/test_classifier_kmeans 均存在 |
| 1646-6 | P3 | nodata_utils.h 所有权 | **已修未关**：consumers 已含 resampler/spectral_anomaly/spectral_local_rx/segment_features/multires_segmenter/raster_histogram/sar_calibration | grep bandNoDataSentinel/isNoDataValue | 建议关闭 |
| 1467（部分） | P1/P2 | rs_spatial_cross_validation.h 双实现 vs src/core/spatial_split.cpp | **跨轨**：spatial_split.cpp 在 src/core（禁区），analysis 侧单独修会造成两轨冲突 | — | 登记 CROSS_TRACK_FINDINGS |

## B. 跨轨 issue（登记移交，本轨不修）

- **#1541** [P1] detection_tile_engine.cpp:323 缺 CV_CN_MAX 护栏 — `src/operators/runtime/`（02 轨禁区；兄弟引擎 tile_inference_engine.cpp:1131 已有 typed refusal 可对齐）
- **#1642** [P3×4] operators/framework registry 锁内调工厂；operators/runtime const_cast；operators/io schema string-/array 不符（15 处）— 02/03 轨
- **#1646** 其余 5 项 — processing/framework（GDALAllRegister 绕过、local_worker_pool 无锁读）、processing/gdal（move 丢 lastError）、json_params_converter 重复声明、algorithm_help_catalog 14 死键 — 02/03 轨
- **#1573/#1575/#1576/#1577** — operators/runtime TensorRT/OpenVINO/schema setRange — 02 轨
- **#1467** 主体 — src/core temporal_cube 静默 NaN、radiometric_state GDALOpenEx、qgsdatasourceresolver — 02 轨
- **#1565-#1569** — src/processing providers（GenericCliProvider/new Json::Value/端口类型/外部工具诊断）— 处理框架域，非本轨算法绑定，归 02/03 轨
- 其余 P2/P3（app/gui/agent/cli/workflow/verify/teaching 等）— 对应 03/04/05 轨

## C. 本轮原创发现（未入 issue，将由本 PR 修复）

详见 TASKS.md 的 F-01..F-07（三路独立只读审查 + 主 Agent 逐行复核确认）。
