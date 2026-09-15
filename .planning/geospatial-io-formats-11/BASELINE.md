# BASELINE — Phase 0 启动审计（2026-09-16，只读）

## Git / GitHub facts（启动时实测）

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  `fix: fail-closed fixes for review issues #994–#999 (#1000)`。
- **Prompt 快照已过时**：快照 SHA `ebcafb4d02`（#990）之后 master 新增：
  - D18 Unified Mission Workbench (#991 merged, c5d4aafe8e)
  - D19 Dataset Foundry & Benchmark (#992 merged, 1cea98921b)
  - #993 CI compile fixes (macOS/Windows d17/Win32)
  - **#1000 fail-closed fixes for review issues #994–#999**（a5b11b7f10，HEAD）
- Remote branches（非 itk-upstream）：仅 `origin/zcode/execution-runtime-convergence-11`（= PR #1009 head）、`origin/zcode/radiometric-spectral-workbench`（= PR #1008 head）、master。
- 本地 worktrees：34 个既有 track worktree；本 track 新建 `../exp-rs-geospatial-io-formats-11`。

## Open PRs（启动时全部）

| PR | title | head | base | mergeState | 与本 track 交集 |
|---|---|---|---|---|---|
| #1009 | execution-11 scientific execution runtime convergence | `zcode/execution-runtime-convergence-11` | master | UNSTABLE | 无业务交集。touches `src/runtime/chunk/**`, `src/runtime/exec/**`, `src/runtime/worker/**`, `src/operators/framework/**`, `src/processing/framework/**`, `src/workflow/pipeline_run_coordinator.cpp`, `src/agent/data_platform_tools.cpp`, tests `test_chunk_*`/`test_execution_*`。共享文件仅 `src/operators/CMakeLists.txt`, `tests/CMakeLists.txt`, `CHANGELOG.md`, `.gitignore` → append-only 接线。其 `src/runtime/chunk/` 下已有 `resumable_tile_run.*`？——**PR diff 中有 `src/runtime/chunk/resumable_tile_run.{cpp,h}`（master 上尚不存在）**，属于 G01 chunk 级 resume，不是 dataset 级 stage resume；本 track 的 dataset-level stage/attach/repair seam（包 F）不与其重叠，但 PR_BODY 声明边界。 |
| #1008 | spectral Day 13 radiometric/6S/spectral workbench | `zcode/radiometric-spectral-workbench` | master | DIRTY | 无业务交集（src/core/spectral, src/analysis/*, src/app/widgets, src/agent/spatial_tools, processing/algorithms）。共享文件仅 CMakeLists/CHANGELOG/.gitignore。注意其 `radiometric_state` 与本 track metadata truth 的 radiometricState 字段仅通过既有 ADR 0114 authority 关联，不新增。 |

结论：两个 open PR 均不触碰 `src/geospatial/io/**`（不存在）、`src/geospatial/cog/**`、`src/geospatial/metadata/**`、`src/geospatial/vector/**`、`src/operators/io/io_operators.cpp`、`docs/io/**`。本 track primary scope 与二者无文件级冲突。

## Open issues（启动时全部 7 条，逐条 dedupe）

| Issue | 标题摘要 | 判定 |
|---|---|---|
| #1001 | **[bug][io] io:clip uses srcCrsOverride as targetCrs even when input already has a CRS (silent wrong clip)**, critical/P1 | **IN SCOPE，本 track 修复**。落点 `src/operators/io/io_operators.cpp` IoClipOperator（:363-403，master 现状：srcCrsOverride 被赋给 `options.targetCrs`，`sourceCrsOverride` 从未设置，与 io:reproject F-OPS-4 修法不一致）。无任何 open PR 认领。 |
| #1002 | [bug][workflow] makeRegistryNodeExecutor reports success without verifying artifact file exists | OUT_OF_SCOPE（workflow/agent 层，非 I/O 库；不与 #1009 重复实施——该 PR 改 pipeline_run_coordinator 但 issue 指向 registry node executor）。 |
| #1003 | [bug][dataset] joinFeaturesBySampleId treats JSON-null required columns as present | OUT_OF_SCOPE（D19 dataset foundry 业务）。 |
| #1004 | [bug][agent] dataset:qa identity Passes uniqueness when scan_capped (#996 residual) | OUT_OF_SCOPE（agent dataset:qa 工具）。 |
| #1005 | [bug][georef] mapPickToLayerCrs returns untransformed canvas point when CRS transform throws | OUT_OF_SCOPE（GUI georef dock）。 |
| #1006 | [bug][workflow][tests] PipelineRunCoordinator soft-defaults to syntheticExecute (#999 residual) | OUT_OF_SCOPE（workflow 层；#1009 相关但不属于 I/O track）。 |
| #1007 | [bug][dataset] dataset:qa never audits CRS despite empty schema.crs | OUT_OF_SCOPE（D19 dataset QA），但其“CRS 缺失必须显式呈现”原则与 io:inspect 的 `crs.valid` 模型一致——canonical metadata 已满足；记录证据即可。 |

`ISSUES.md`（D3 lab-content backlog）：T-1/T-2/T-3/S-1/S-2/H-1/H-2/H-3/C-1/C-2 —— 经对 master 核验：T-1/T-2/T-3/C-2 已由 temporal 10.0 修复（CHANGELOG `[Unreleased] Temporal Platform 10.0` 明确列出 T-1/T-2/T-3/C-2），其余为 temporal/SAR/高光谱/制图业务缺口，**均不在本 track scope，不实施**。

## 代码现状（详细 inventory 见 subagent #1 审计报告，要点）

**已存在（不重建）**：
- `src/geospatial/util/atomic_fs.*`：stagedPathFor / fsyncFile / publishStagedFile / publishStagedGroup（sidecars-first、main-last、`.bak` backup set + rollback #791）/ discardStaged / writeFileAtomic；消费者：RasterWriter、VectorWriter、raster_convert（translate/warp/makeCog）、doctor、fabric/mirror、remote/range_cache_disk、cli。
- `RasterWriter`（staged、metadata-before-pixels、integer clamp refusal、finalize=reopen shape-validate→publish、cancel/dtor cleanup）；`VectorWriter`（staged group、事务批、fail-closed driver）。
- COG：`cog_presets`（5 preset，BIGTIFF=IF_SAFER、OVERVIEWS=AUTO、NUM_THREADS=ALL_CPUS、BLOCKSIZE=512 硬编码）+ `makeCog`（pre-publish validateCog，fail 弃 staging）；`cog_validator`（tiled/blocksize≥128 pow2/overviews 深度/compression/PREDICTOR advisory）。
- Vector：VectorReader 流式批、VectorWriter、vectorConvert（explicit CRS、WHERE、clip-before-transform）、FormatRegistry driver gating + certified profiles（GPKG/GeoJSON/GeoJSONSeq/FlatGeobuf/GeoParquet）。
- `canonical_metadata`（scale/offset/unit/band role/wavelength/colorInterp/subdatasets/product semantics，JSON-symmetric）+ inspectRaster/Vector/Multidim/Any。
- `resource_uri`（parse/canonical/display redacted/resolveAgainst traversal 拒绝/toWindowsLongPath）、`object_store` ScopedObjectStoreCredentials RAII、`gdal_compat.h` 版本宏、`sha256` 流式、`multidim_view/cube`、`data_doctor`、io:* 十算子 + io:catalog_search/cube_plan/cube_window/cache_prefetch。
- 测试：Catch2、`sicnu_add_io_test` helper、driver-gated skip 惯例、`tests/test_io_*` 30+。

**真实缺口（本 track 的 rescope 依据）**：
1. **A/F**：finalize 无内容 digest、无 finalize manifest/sidecar（provenance receipt）；无 dataset 级 attach-existing/stage-ledger/orphan sweep API（crash 后 staged 文件只能手工发现）；RasterWriter attach 不存在。
2. **B**：COG 无 blocksize/overview forcing/NoData/alpha 选项层；无 deterministic（NUM_THREADS=1）模式；validator 无 corrupt/truncated COG 负向测试 fixture；docs/io/cog-guide.md 声称 `OVERVIEWS=ALL` 与代码 `AUTO` 漂移。
3. **C**：`io:convert_format` 硬编码 5 driver 名单而非 FormatRegistry capability 查询；GeoParquet 写入无 capability 报告面。
4. **D**：无 subdataset inventory/selection 操作面（只有 io:inspect 被动列出）；ResourceUri 已有 safe URI 但 operator 未暴露。
5. **E**：无 metadata 写回/patch API（roundtrip 仅 at-create）；无 provenance sidecar。
6. **G**：operator 参数层接受裸路径，未接 resource_uri 守卫。
7. **H**：无 GDAL feature-detection 报告测试、无 corrupt-GPKG/truncated-COG 套件。
8. **#1001**：io:clip srcCrsOverride 语义 bug（P1，critical label）。

## 环境事实

- 构建 preset：`dev-default`（Debug, ENABLE_TESTS=ON, binaryDir build-dev）。
- 资源上限：build `-j2`（CMAKE_BUILD_PARALLEL_LEVEL=2），ctest `-j1`。
- 本 track 未运行前，master 上 I/O 套件的本地基线需在 worktree 内先验证（部分 Qt 套件与 GUI 相关，本 track 只 gate `test_io_*` 家族）。
