# PARALLEL_OWNERSHIP — F15 mosaic-fusion-11

规则：仍开放 PR 的 changed files 默认 read-only；本 track 业务主体落在 GOAL 声明的 primary scope。

## 启动时 open PR 与本 track 的文件级交集

### PR #1008 `zcode/radiometric-spectral-workbench`（open, DIRTY）

| #1008 changed file | 本 track 是否触碰 | 策略 |
|---|---|---|
| `src/processing/algorithms/radiometric_calibration.*` | 否 | read-only；本 track B 包为镶嵌相对匀色，新建 `mosaic_balancing.*`，不复用不改名复制 |
| `src/processing/algorithms/spectral_indices.*` / `spectral_unmixing.*` | 否 | read-only |
| `src/core/radiometric_state.*` / `spectral_library.*` | 否 | read-only |
| `src/analysis/{atmospheric,hyperspectral}/*` | 否 | read-only |
| `src/agent/spatial_tools/*`（含 `spatial_tool.cpp`） | 否 | 本 track agent surface 仅经 operator registry（rs_operators_init），不碰 spatial_tool.cpp |
| `src/app/widgets/*` / `src/app/dialogs/*` | 否 | 本 track GUI surface 不改动（GUI 镶嵌对话框走既有 rs:mosaic，保持兼容） |
| `tests/CMakeLists.txt` | **是（共享）** | append-only 追加我的 test target，集中在文件尾部一次 commit，减小冲突面 |
| `.gitignore` | 否 | 不碰（`.planning/*` 已被忽略，按 #1008 同款 `git add -f` 跟踪本 track planning） |
| `src/processing/CMakeLists.txt` / `src/operators/CMakeLists.txt` | **是（共享）** | #1008 未改这两个文件（其改的是 analysis/core/app/agent/tests 的 CMake）；仍保持 append-only |
| `docs/adr/` | 新编号 | #1008 用 0158；master 已有 0162（D17）。本 track 新 ADR 取 **0163** |

其余 open issues #1001–#1007：均不在 mosaic/fusion 域，不实施、不 dedupe 修复。
#991/#992 已合并 → master 即事实源，无 read-only 约束遗留（但仍遵守 GOAL 的 D18/D19 read-only 排除项：
`src/workflow/`、`src/dataset/`、`src/experiment/` 本 track 不写）。

## 本 track primary write scope（收窄后）

- `src/processing/algorithms/mosaic_plan.{h,cpp}`（新）
- `src/processing/algorithms/mosaic_balancing.{h,cpp}`（新）
- `src/processing/algorithms/mosaic_seamline.{h,cpp}`（新）
- `src/processing/algorithms/mosaic_blend.{h,cpp}`（新）
- `src/processing/algorithms/mosaic_quality.{h,cpp}`（新）
- `src/processing/algorithms/fusion_quality_report.{h,cpp}`（新）
- `src/operators/rs/rs_quality_mosaic_operator.{h,cpp}`（新）
- `src/operators/rs/rs_image_fusion_operator.{h,cpp}`（既有，追加 gram_schmidt/hpf 方法 + qualityReport 参数）
- `tests/test_mosaic_*.cpp`、`tests/test_fusion_quality_report.cpp`、`tests/test_quality_mosaic_operator.cpp`、`tests/mosaic_test_utils.*`（新）
- `docs/processing/mosaic_fusion.md`（新）+ `docs/adr/0163-*.md`（新）

## 共享 integration files（append-only 最小接线）

`src/processing/CMakeLists.txt`、`src/operators/CMakeLists.txt`、`src/operators/rs/rs_operators_init.cpp`、
`src/agent/harness/capability_catalog.cpp`、`src/contracts/scientific_contract.cpp`、
`tests/CMakeLists.txt`、`CHANGELOG.md`。

## 明确 read-only（GOAL 声明 + 并发协调）

- G01 runtime internals、`src/runtime/`
- D18（MissionContext/workbench）、D19（dataset foundry/benchmark）：`src/workflow/`、`src/dataset/`、`src/experiment/`
- PR #1008 全部 changed files（上表）
- `src/processing/algorithms/pansharpening.*`：F 包只 include 其头文件复用 `PanSharpening::evaluateQuality`，
  不修改（虽无并发 PR 占用，保持最小 diff 便于 review）
- `src/core/`（vendored QGIS，ADR 0159 先例禁止）
