# BASELINE — spectral-intelligence-11

Phase 0 只读审计，2026-09-15，从主仓库执行（所有命令均为只读）。

## Git 事实（启动时刷新）

- `git fetch origin --prune` → clean（无新 remote branch 提示输出）。
- `git rev-parse origin/master` → **`a5b11b7f10fa010c1c060864fb427d777ba9a4aa`**
  （= "fix: fail-closed fixes for review issues #994–#999 (#1000)"）。
- 注意：Prompt 生成时的快照 `ebcafb4d` 已过期。**#991 (D18 unified mission workbench) 与
  #992 (D19 dataset foundry/benchmark) 均已合并进 master**（见 `git log`：
  `1cea9892 Merge ... d19 (#992)`、`c5d4aafe D18 ... (#991)`）。按规则以新 master 为事实源。
- Remote branches：`origin/master`、`origin/zcode/radiometric-spectral-workbench`（= PR #1008 head）。

## Open PR（启动时）

| PR | head | base | state | mergeState | 内容 |
|---|---|---|---|---|---|
| #1008 `feat(spectral): Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench` | `zcode/radiometric-spectral-workbench` | master | open, not draft | **CONFLICTING**（与 master 冲突，需其自行 rebase） | 辐射状态 FSM、6S LUT 大气校正、光谱指数、continuum removal、FCLS 重写、VCA、`src/core/spectral_library`、`src/app/widgets/spectral_profile_widget` 改造、band palette、agent 物理工具 |

`gh pr diff 1008 --name-only`（changed files，对我 read-only）：
`.gitignore`, `.planning/radiometric-spectral-workbench/**`, `docs/adr/0158-*`,
`src/agent/CMakeLists.txt`, `src/agent/spatial_tools/spatial_tool.cpp`,
`src/agent/spatial_tools/spectral_spatial_tools.{h,cpp}`, `src/analysis/CMakeLists.txt`,
`src/analysis/atmospheric/fast_6s_lookup.{h,cpp}`, `src/analysis/hyperspectral/continuum_removal.{h,cpp}`,
`src/app/CMakeLists.txt`, `src/app/widgets/band_composite_palette.{h,cpp}`,
`src/app/widgets/spectral_profile_widget.{h,cpp}`, `src/core/CMakeLists.txt`,
`src/core/radiometric_state.{h,cpp}`, `src/core/spectral_library.{h,cpp}`,
`src/processing/algorithms/radiometric_calibration.{h,cpp}`,
`src/processing/algorithms/spectral_indices.{h,cpp}`,
`src/processing/algorithms/spectral_unmixing.{h,cpp}`, `tests/CMakeLists.txt`,
`tests/test_{continuum_removal,d13_radiometric_spectral_e2e,fast_6s_atmospheric,radiometric_calibration,radiometric_state,spectral_agent_tools,spectral_indices,spectral_library,spectral_profile_widget,spectral_unmixing_fcls}.cpp`

**Dedupe 判定**：#1008 不做 local/dual-window RX、sparse unmixing、SID-SAM hybrid、
端元聚类/光谱角矩阵、MNF→PPI artifact 链路深化、256–1024 band 规模硬化。
其 VCA/FCLS/library/profile work 与本 track 互补但文件重叠（尤其 `spectral_unmixing.*`、
`spectral_profile_widget.*`、`tests/CMakeLists.txt`、`src/app/CMakeLists.txt`、`.gitignore`）。
→ 本 track **全部新文件 + 共享注册文件最小 append-only**，不触碰上表任何路径的业务内容。

## Open issues（启动时）

#1001–#1007 全部为 R2 审查发现的 dataset/workflow/georef/io/agent 缺陷
（io:clip CRS、mapPickToLayerCrs、makeRegistryNodeExecutor、joinFeaturesBySampleId、
dataset:qa、PipelineRunCoordinator syntheticExecute、dataset:qa CRS audit）。
**无一是 spectral 域**；不在本 track 修复，不重复实现（dedupe 完成，均为其他域 track 责任）。

`ISSUES.md` = 旧 D3 backlog（temporal/SAR/lab 内容）。其中 H-1（libraryPath）、H-2（PPI 结果
管道消费）、H-3（inverse MNF）已被 10.0 修复（见 10.0 CAPABILITY_MATRIX 与
`rs_mnf_inverse_operator`、`rs_library_select_operator`、`rs_spectral_reference_input` 在
master 上的存在）。不作为本 track backlog。

## master 光谱现状（10.0 遗产）

- 全局 RX（`SpectralAnomaly::rxDetector`、streaming `BackgroundStats`、ridge 1e-9、
  NoData 语义）— `src/processing/algorithms/spectral_anomaly.{h,cpp}`。
- LS+ridge `unmix` 与 penalty-FCLS `unmixFcls`（Lawson-Hanson NNLS + sum-to-one 罚）—
  `src/processing/algorithms/spectral_unmixing.{h,cpp}`。
- SAM `spectralAngle`、SID `spectralDivergence`、`samClassify`/`sidClassify`、
  `continuumRemoval` — `src/processing/algorithms/spectral_classification.{h,cpp}`。
- 匹配滤波 MF + ACE — `src/processing/algorithms/spectral_detection.{h,cpp}`。
- PPI `pixelPurityIndex` — `src/processing/algorithms/endmember_extraction.{h,cpp}`。
- 流式 MNF forward/inverse + model artifact（`exp-rs:mnf-transform`）— `mnf_transform.{h,cpp}`。
- 库 domain（Library/Entry/SensorProfile/license/validate）— `spectral_library.{h,cpp}`；
  表 artifact（`exp-rs:spectral-table`，provenance+digest）— `spectral_table.{h,cpp}`；
  波长 grid/单位归一 — `spectral_wavelength.{h,cpp}`；ROI 光谱 — `spectral_roi.{h,cpp}`。
- 算子：`rs:rx_anomaly`(global only)、`rs:sam_classify`、`rs:spectral_unmixing`(OLS+FCLS)、
  `rs:endmember_extraction`(PPI)、`rs:mnf`/`rs:mnf_inverse`、MF/ACE detection 算子、
  `rs:library_select`、`rs:spectral_resample` 等，注册于
  `src/operators/rs/rs_operators_init.cpp`（REGISTER_RS_OPERATOR 宏 ~L121 + factory ~L285）。
- capability meta：`data/processing/algorithm_meta/capability/rs-*.json`；
  agent 能力 `data/agent/capabilities/spectral.json`；库数据 `data/spectral/{library.json,
  library.schema.json,sensors.json,LICENSES.md}`。
- ADR：0076 SID、0077 unmixing、0078 RX、0079 resampling、0080 PPI、0081 library、
  0082 wavelength profile、0092 library matching workbench。

## 真实缺口（本 track 的 justification）

1. RX 只有全局背景：无 local/dual-window RX、无窗口边界/halo 流式、无异常质量/置信输出。
2. 解混无稀疏约束：无 L1（和为一/非负可组合），无病态端元集检测（FCLS 只拒绝共线）。
3. SAM 与 SID 并存但无 hybrid 相似度（无尺度/概率域融合定义、无 known-answer）。
4. 端元侧只有 PPI：无聚类/去冗余、无端元×端元光谱角矩阵、无 sensor 投影保留 provenance 的一等 artifact。
5. Workbench：master GUI 有 spectral curve/profile/library dialog；无端元矩阵/异常图层联动面板。
6. 256–1024 band 规模证据缺失（10.0 只到 256-band synthetic）。

## 本 worktree

- `../exp-rs-spectral-intelligence-11`，branch `zcode/spectral-intelligence-11`，
  基于 `origin/master` = `a5b11b7f`（Phase 0 审计后创建）。
