# R25-B BASELINE — 执行时实时侦察记录

- 侦察时间：2026-10-28 14:03 UTC（执行时实查，非快照）
- 仓库：`https://github.com/WindWang2/exp-rs`
- `origin/master` = `2610cb4741e69a89124921e44c03e340e2522ee2`（与快照一致；2026-10-27 16:49:07 UTC，= PR #1723 merge）
- Open PR：**0**（`gh pr list --state open` 实查）
- Open Issues：**144**（`gh issue list --state open --limit 300` 实查，全量 JSON 存档于本机会话临时目录）
- 源 worktree 干净（仅未跟踪 `.workbuddy/`）；未在源目录开发。
- Worktree：`../exp-rs-r25-B-20261028T140313Z`；分支：`agent/r25-B-data-model-runtime-20261028T140313Z`；BASE_SHA 同上。

## 宿主环境（Linux 共享机）

- CMake 4.4.4 / g++ 16.2.1 (GCC) / Ninja / Qt 6.12.0 / GDAL 3.13.3 / SQLite OK
- **并发兄弟轨实况**：R25-A（terrain）、R25-C（agent）worktree 同时在本机构建，共用 `flock ~/.cache/exp-rs-build.lock`。本轨构建串行排队，`CMAKE_BUILD_PARALLEL_LEVEL=2`、`ctest -j1`。
- 构建配置：`cmake -S . -B build-r25-B -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DSICNU_VENDOR_GDAL=OFF -DSICNU_WITH_ONNX_RUNTIME=OFF -DSICNU_BUILD_OTB=OFF -DSICNU_EMBED_PYTHON=OFF -DSICNU_TOLERANT_TEST_DISCOVERY=ON`（沿用 r23-B 已验证旗组；configure EXIT=0）

## 已合并的前序 B 轨工作（不重做）

- **PR #1719 (r24-B)**：检测 NoData 哨兵→NaN、tile 尾批 flush、DatasetStore 六写路径事务门、#1581 lineage 方向保持重挂、eo_preflight advisory 化、WAVELENGTH_UNITS 归一、publish fence（唯一 stage 名+QLockFile）、leakage bucket UB 护栏、open() schema 门三态、MSVC OOM 针词。
- **PR #1723 (r23-B)**：OV/TRT stub 编译门、derived output head 契约门（`ensureDerivedModeMatchesHeadContract`）、`upsertRunWithMetric` 单事务、probeRemote per-hop guard（httpFetchStatus + DNS pinning）。
- 测试注册块：`# BEGIN R24-B` / `# ── R23-B` append-only 块在 `tests/CMakeLists.txt` 末尾，本轨只追加 `# BEGIN R25-B` 块。

## 侦察结论（B 域三个深挖点，均经实码验证存在缺口）

1. **导入预检解释报告（W1）不存在**：`DataManager::registerSource`（src/data/data_manager.cpp:452-545）一步解析+提交；唯一机器可读通道是扁平 `QVector<Diagnostic>`（code/message/severity，src/data/data_result.h:19-24）。CN 产品有 dry-run（rs_product_import_plan.h）、collection 有 probe（collection_import_service.h:179），但**普通文件导入（单 GeoTIFF/ENVI/多波段/损坏产品）无任何预检报告**；无歧义候选列表（registry 首匹配即赢）；`assetIdentityToken`（li1/ri1）从未进入导入路径；批量导入只有 ok/failed 计数（main_window_project.cpp:529-568，D 轨文件，只读）。数据 doctor（geospatial/doctor）存在但从不被导入路径调用。
2. **可解释推理计划（W4）不存在**：`model_execution_service.h` 是唯一执行 seam（resolve→gates→readiness→session→engine→payload），但**没有 plan/dry-run 入口**（无资源估计/瓦片网格预算/输出模式×head 契约预检/警告聚合的机器可读结构）。运行后质量信息散落在 `TileInferenceStats`（classPixelCounts/eoPreflight/inputGrids）与 payload，无独立类型化「质量摘要」结构。
3. **复现差异原因报告（W5）半成品**：`ReplayDeviationAnalyzer`（src/experiment/replay_deviation.cpp:93-189）有类型化 verdict（identical/equivalent/deviated/incomplete）+ pinComparison + metricDelta + environmentDrift，但 `deviations` 是**扁平字符串**——无领域归属（实验/数据/模型/环境）、无每个差异的可操作修复建议、schema v1 无结构化条目。

## 本轨写入边界（单写者）

- 写：`src/data/`（新 import preflight 模块）、`src/operators/runtime/`（新 inference plan/quality 模块）、`src/experiment/`（replay_deviation 扩展）、`tests/test_r25_b_*.cpp` + `tests/CMakeLists.txt` 末尾追加块、`docs/`（对应文档）、`models/` manifest（如需）、`.planning/zcode-r25-B/`、`.gitignore`（本轨 allow-list 块）。
- 只读：`src/app/`（D 轨）、`src/agent|workflow|jobs|sdk|pi`（C 轨）、`src/operators/rs/`+`src/analysis/`（A 轨）、`src/lab|teaching*|verification`（E 轨）、`src/core|gui|external`。
