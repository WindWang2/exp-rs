# PARALLEL_OWNERSHIP — 启动时（2026-09-16）快照

## Open PR / remote branch ownership

| Source | Changed files（业务主体） | 与本 track primary scope 交集 | 策略 |
|---|---|---|---|
| PR #1009 `zcode/execution-runtime-convergence-11` | src/runtime/chunk/**、src/runtime/exec/**、src/runtime/worker/**、src/runtime/observability/**、src/operators/framework/**、src/processing/framework/**、src/workflow/pipeline_run_coordinator.cpp、src/agent/data_platform_tools.cpp、tests/test_chunk_* / test_execution_*、CHANGELOG.md、.gitignore、src/operators/CMakeLists.txt、tests/CMakeLists.txt | 业务主体：无。共享 integration：`src/operators/CMakeLists.txt`、`tests/CMakeLists.txt`、`CHANGELOG.md`、`.gitignore` | 这些共享文件我只做 **append-only 最小接线**；rebase 冲突时按行上下文手工合流，不用 ours/theirs。其 `resumable_tile_run.*` 为 G01 chunk 级 resume（read-only，不复制）；本 track dataset 级 stage seam（包 F）在 `src/geospatial/io/`，PR_BODY 声明边界互补。 |
| PR #1008 `zcode/radiometric-spectral-workbench` | src/core/{radiometric_state,spectral_library}.*、src/analysis/{atmospheric,hyperspectral}/**、src/app/widgets/{band_composite_palette,spectral_profile_widget}.*、src/agent/spatial_tools/**、src/processing/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}.*、tests/test_*spectral* 等 | 业务主体：无。共享：CMakeLists ×3、CHANGELOG.md、.gitignore | 同上 append-only。其 `src/core/radiometric_state` 是 radiometricState 语义 authority（ADR 0114/0158）；本 track E 包 canonical metadata 只消费既有 `RadiometricState` 字符串词汇，不改其定义。 |

## 本 track 独占（无 open PR 触碰，启动时核验 `gh pr diff --name-only`）

- `src/geospatial/io/**`（新建目录）
- `src/geospatial/cog/**`（无 PR 触碰；本 track 需最小 additive 扩展 validator/presets 选项层 —— DECISIONS D-003 记录 scope 扩大理由）
- `src/operators/io/io_operators.cpp/.h`（io:clip 修复 #1001 + 新算子接线）
- `docs/io/**`
- `tests/test_io_*`（新增测试文件；既有 io 测试只在其覆盖的旧行为变化时最小修改）

## Master 已合并、视作权威 seam 的近期能力（消费，不重做）

- #1000（a5b11b7f10）：review #994–#999 fail-closed 修复 —— 我 rebase 到此 SHA 之上开发。
- atomic_fs family（#791/#807 语义）、F-OPS-4（io:reproject sourceCrsOverride）、FormatRegistry、resource_uri、object_store、canonical metadata ADR 0114。

## Issue dedupe 证据

#1001 → 本 track 修复（src/operators/io/io_operators.cpp + tests/test_io_operators.cpp）。
#1002–#1007 → OUT_OF_SCOPE，理由见 BASELINE.md；#1007 的“CRS 缺失显式呈现”原则由 canonical_metadata `crs.valid` 既有事实满足，记录不实施。
