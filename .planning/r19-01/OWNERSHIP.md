# R19-01 OWNERSHIP — 空间数据 × 模型运行底座

基线：`origin/master = e12a9afbc8883a474dc9ffcaf3345ae55863dc71`（2026-10-19T12:35:16Z merge #1662）
分支：`agent/r19-01-data-model-runtime-20261020T102418Z`（自基线新建）
Worktree：`/home/kevin/project/exp-rs-01-data-model-runtime-20261020T102418Z`

## 本轨可写（任务书 §专项01 ownership）
- `src/geospatial/**`（io/remote/stac/cog/fabric/identity/products/util/multidim/…）
- `src/data/**`（资产身份、溯源、artifact store 数据端、governance）
- `src/dataset/**`、`src/experiment/**`
- `src/runtime/**`（chunk/gpu/exec/observability/worker）
- `src/operators/runtime/**`、`src/operators/io/**`、`src/operators/framework/chunked_run.*`
- `src/operators/framework/**` 其余文件：仅在无竞争且问题实证时最小改动（当前候选：#1578 setRange 运行时强制，落点 rs_json_params/rs_schema）
- `src/operators/rs/rs_continuum_removal_operator.cpp`、`rs_spectral_resample_operator.cpp`（#1544 已修，只需复核；若复发才动）
- 对应测试：`tests/test_io_*`、`test_chunk_*`、`test_model_*`、`test_dataset_*`、`test_experiment_*`、`benchmarks/`
- `.planning/r19-01/**`

## 只读 / 临界区
- `tests/CMakeLists.txt`、根 `CMakeLists.txt`、`.gitignore`、`CLAUDE.md`、`docs/adr` 索引、共享脚本：
  **公共文件**。开放 PR #1664 已改 `tests/CMakeLists.txt` 与 `.gitignore` —— 我方如需注册新测试，
  先用最新 origin/master 定向 reconcile 再最小 append。禁止全文件重排。
- `src/cli/**`：不属本轨写集；#1597/#1595 已在 master 修复（本文件记录核实结论），无需动。

## 其他轨锁定（绝不写入）
- **R18-A（开放 PR #1663, head b5e9e6af1）**：`src/analysis/segmentation/rs_multires_segmenter.*`、
  `src/operators/rs/{rs_mosaic_operator,rs_raster_spatial_operators,rs_sam_classify_operator}.cpp`、
  `src/processing/algorithms/{atmospheric_correction,chunked_processor,image_fusion,mosaic}.cpp`、
  其 PR 改动测试、`.planning/r18-a*`
- **R18-E（开放 PR #1664, head bddbff58f）**：`src/teaching/**`、`src/teaching_admin/**`、`src/lab/**`、
  `src/grader/**`、`src/verification/**`、`src/faultlab/**`、`src/experiment_studio/**`、
  `src/cli/lab_batch_runner.*`、`src/help/help_content_store.cpp`、`data/faultlab/faults.schema.json`、
  其 PR 改动测试、`.planning/r18-e*`
- 轨 02（Pi/MCP/agent/workflow/plugins/pi/**）、轨 03（app/ui/gui/cartography）
- 并行 R19 轨（r19-02 pi-mcp-workflow / r19-03 desktop 等 worktree、分支、构建目录、缓存/PR）：
  禁删禁改；全局构建锁 `$GIT_COMMON_DIR/r19-global-j2-build` 已实测跨轨串行生效

## 候选任务 → 上游状态映射（2026-10-20 LIVE 核实）
| path/issue | 上游 | master blob 状态 | 处置 |
|---|---|---|---|
| #1545 range_cache SSRF propagation | #1662 已合并 | 已修（range_cache.cpp:292 `options.blockPrivateNetworks = config.blockPrivateNetworks`）| 只补测试证据，不复修 |
| #1548 identity bridge 死锁 | #1662 已合并 | 待测（O-A1 种子 1 复现）| 复现/纠偏 |
| #1553 ChunkGraph::addSink | #1662 已合并 | 已修（chunk_graph.cpp:92-104 走 requireFreeConsumerLocked）| 测试钉住 |
| #1554 ModelSessionPool 析构 | #1662 已合并 | 已修（gpu_plane.cpp:44-64 持锁）| 测试钉住 |
| #1603 throttleWait 无界 | #1662 已合并 | 已修（execution_governor.cpp:561-610 deadline+maxPolls）| 测试钉住 |
| #1604 FileTraceSink 重建 | #1662 已合并 | 已修（trace.cpp:267 thread_local ofstream）| 测试钉住 |
| #1605 事件环 erase 1/4 | #1662 已合并 | 已修（execution_telemetry.cpp:103-115 就地覆写+游标）| 测试钉住 |
| #1546 repeat_execution 遮蔽 | #1662 已合并 | 已修（内层声明已删）| 测试钉住 |
| #1579 resolveAgainst '%' | R15-B #1505 | 已修（resource_uri.cpp:966-984）| 跳过 |
| #1580 bundle run_id 未校验 | sweep | 已修（import :348 RunId::fromString）| 跳过 |
| #1581 provenance.json 丢弃 | sweep | 已修（import :447-478）| 跳过 |
| #1582 评测跳行无计数 | sweep | 已修（coverage accounting）| 跳过 |
| #1583 auditFolds 基数 | sweep | 已修（按 assignments 推导）| 跳过 |
| #1584 overlap 只查 left | sweep | 已修（双侧门槛）| 跳过 |
| #1585 pair 去重缺口 | sweep | 已修（仅对称去重）| 跳过 |
| #1586 leakage 事务 | sweep | 已修（:247 BEGIN IMMEDIATE）| 跳过 |
| #1587 mirrorAllAssets (void) | sweep | 已修（failed* 计数+警告）| 跳过 |
| #1577 artifactPath 硬编码 | R15-C | 已修（openvino:70/tensorrt:85,148）| 跳过 |
| #1576 OpenVINO traits | — | 文档化设计决策（openvino_provider.cpp:255-270 注释）| 不改，PR 说明 |
| #1544 GDAL 句柄 RAII | sweep | 已修（DatasetFileGuard，continuum:36-46,231）| 复核 spectral_resample |
| #1597 CLI parseOptions | sweep | 已修（leftover 记录）| 跳过（src/cli 非本轨）|
| #1595 cloud-max ok | sweep | 已修（"--cloud-max needs a number"）| 跳过 |
| **#1637-2 format_profiles Zarr/STAC** | 无 | **未修（profileForPath rfind('.') 首匹配，zarr.json/stac json 永不可达）** | **本轨认领** |
| #1637-1/3/4 | — | 已修（virtual_dataset/死代码已删/probe_remote:102 egress 门）—— 但 **3 的孪生 readFileText 无界读仍在** | 孪生项本轨认领（T1.3）|
| #1635 evaluation 重复代码 | 无 | **已过时**（当前代码已是单一 nearInCells + 单一 cellSize，重构已发生）| 跳过 |
| #1578 setRange 仅元数据 | 无 | 未修（72 处声明无运行时强制）| 本轨认领（framework 最小强制点）|
| #1588 preflight temporal | 无 | 未查 | 非本轨（src/preflight），CROSS_TRACK |
| #1589 science_context 接线 | 无 | 未查 | 非本轨，CROSS_TRACK |
