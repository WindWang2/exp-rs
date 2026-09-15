# PARALLEL_OWNERSHIP — cn-eo-product-physics-11

启动时（2026-09-15）所有 open PR / remote branch 的 changed-file ownership 与本 track 的交集。

## Open PR 清单

| PR | Branch | Head | Files 摘要 | mergeable |
|---|---|---|---|---|
| #1008 | `zcode/radiometric-spectral-workbench` | `92fd8091` | `.gitignore`；`.planning/radiometric-spectral-workbench/*`；`docs/adr/0158-*`；`src/agent/{CMakeLists.txt,spatial_tools/spatial_tool.cpp,spatial_tools/spectral_spatial_tools.*}`；`src/analysis/{CMakeLists.txt,atmospheric/fast_6s_lookup.*,hyperspectral/continuum_removal.*}`；`src/app/{CMakeLists.txt,widgets/band_composite_palette.*,widgets/spectral_profile_widget.*}`；`src/core/{CMakeLists.txt,radiometric_state.*,spectral_library.*}`；`src/processing/algorithms/{radiometric_calibration.*,spectral_indices.*,spectral_unmixing.*}`；`tests/{CMakeLists.txt,test_continuum_removal.*,test_d13_radiometric_spectral_e2e.*,test_fast_6s_atmospheric.*,test_radiometric_calibration.*,test_radiometric_state.*,test_spectral_agent_tools.*,test_spectral_indices.*,test_spectral_library.*,test_spectral_profile_widget.*,test_spectral_unmixing_fcls.*}` | CONFLICTING（与 master） |

其他 PR（#991/#992）已合入 master（基线 `a5b11b7f` 内），无独立 ownership。

## 文件级交集分析（#1008 vs 本 track primary write scope）

| 本 track 将写 | #1008 是否触碰 | 策略 |
|---|---|---|
| `src/geospatial/products/**` | **否** | 直接实现，无冲突 |
| `data/products/**` | **否** | 直接实现（sensor_profiles v2 + 新 fixtures） |
| `src/operators/rs/*product*/（rs_product_import_plan、rs_cn_product_import_operator、family 算子）` | **否**（#1008 只改 `src/processing/algorithms/radiometric_calibration.*`，是另一层的 radiometric kernel，不是 product import 算子） | 直接实现；不 import/依赖 #1008 的 `exp_radiometric` API（open PR，不稳定 seam） |
| `src/app/dialogs/product_import_dialog.*`、`src/app/main_window_*`（最小接线） | **否**（#1008 改 `src/app/widgets/spectral_profile_widget.*`、`band_composite_palette.*` 与 `src/app/CMakeLists.txt`） | 对话框直接实现；若需改 `src/app/CMakeLists.txt`，rebase 时按 append-only 处理（已知 #1008 也在改该文件 → 冲突时手工合并、保留双方 target） |
| `src/agent/spatial_tools/*`（若新增 product agent 工具） | #1008 改 `spatial_tool.cpp`（注册）与 `src/agent/CMakeLists.txt` | 最小 append-only 接线；冲突时手工保留双方注册项 |
| `tests/*cn_product*`、新测试文件、`tests/CMakeLists.txt` | #1008 改 `tests/CMakeLists.txt` | 新测试用独立文件名（`test_cn_product_*11`/`test_product_import_plan_*`）；CMakeLists 冲突时手工合并双方 add_executable/测试注册 |
| `docs/products/**` | 否 | 直接写 |
| `.gitignore`（.planning whitelist 追加） | #1008 追加自己的 whitelist 段 | rebase 时合并双方段（append-only） |
| `docs/adr/`：本 track 用 **0159** 起 | #1008 已认领 **0158** | 编号避让，永不使用 0158 |
| `CHANGELOG.md` | 否 | 独立 integration commit 追加 |
| `src/core/radiometric_state.*`、`src/analysis/**`、`src/processing/algorithms/radiometric_calibration.*` 等 | **是** → 本 track **read-only** | 本 track 不做通用辐射定标 kernel（那是 #1008 的业务主体）；product 层只携带 declared coefficients（现状已是如此），物理量 provenance 由本 track 的 registry/schema 承担 |

## 规则执行记录

1. #1008 changed files 默认 read-only ✅（上表第三列）。
2. 本 track 不依赖 #1008 的任何新 API（`exp_radiometric::RadiometricState`、`Fast6sLookup` 等均不使用）；校准语义沿用 master 已有的 declared-coefficient 契约（`rs_product_import_plan.h` + `docs/products/cn-satellites.md`）。
3. 若 #1008 在本 track 期间合入：重新 `git fetch && git log origin/master` 审计，把新增 master 面纳入 rebase 事实；不保留旧假设。
4. 新出现的并发 PR：提交/Phase 边界时复查 `gh pr list`，同样处理。
5. Open issues #1001–#1007：逐条 dedupe —— 均非 product 领域（dataset/workflow/georef/io:clip），不实施、不修复、不重复实现（证据见 BASELINE.md）。

## 更新（2026-09-16，Phase 8 rebase 前复查）

启动时新出现的并发 PR（均在 a5b11b7f 之上，MERGEABLE）：
- #1009 `zcode/execution-runtime-convergence-11`
- #1010 `zcode/mosaic-fusion-11`
- #1011 `zcode/classification-intelligence-11`

文件级交集核查（`git diff --name-only a5b11b7f..<branch>`）：三者均**不触碰**本 track
primary scope（`src/geospatial/products/**`、`data/products/**`、产品算子、对话框、
`docs/products/**`）；仅共享 integration 文件 `CHANGELOG.md`、`tests/CMakeLists.txt`
（classification 另有 `.gitignore` whitelist 段）——全部 append-only 接线，按 runbook
第 5 条在合并窗口手工并段即可，无业务冲突面。#1008 仍 CONFLICTING（其自身与 master
冲突），继续 read-only，无新增交集。
