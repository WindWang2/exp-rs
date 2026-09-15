# BASELINE — F15 mosaic-fusion-11 · Phase 0 只读审计

审计时间：2026-09-15（启动时）。审计位置：主仓库 `/home/kevin/projects/rs-studio/main`（worktree 创建前）。

## origin / GitHub 事实（启动时刷新）

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  （"fix: fail-closed fixes for review issues #994–#999 (#1000)"）
- **比 prompt 快照新**：prompt 快照为 `ebcafb4d02`；其后 master 新增
  `ebcafb4d02 → #990 fix(ci) → D19 系列 → #991 (D18 merge) → #992 (D19 merge) → #993 fix(ci) → a5b11b7f10 (#1000)`。
- Prompt 中提到的 open PR **#991（D18 Mission Workbench）与 #992（D19 Dataset Foundry）均已合并进 master**
  （`c5d4aafe8e`、`1cea98921b`）→ 按规则 3，不再当作并行开放 PR，master 现状即事实源。
- 本 track 分支基于 `a5b11b7f10` 创建。

## 启动时 open PR（全部）

| PR | branch | title | 状态 | 与本 track 交集 |
|---|---|---|---|---|
| #1008 | `zcode/radiometric-spectral-workbench` | D13 radiometric calibration / 6S / spectral workbench | open, mergeStateStatus=**DIRTY**（与 master 有冲突） | 见下 |

PR #1008 changed files（read-only 对本 track）：`src/core/radiometric_state.*`、`src/core/spectral_library.*`、
`src/analysis/{atmospheric,hyperspectral}/*`、`src/processing/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}.*`、
`src/agent/spatial_tools/spectral_spatial_tools.*` + `spatial_tool.cpp`、`src/app/widgets/{spectral_profile_widget,band_composite_palette}.*`、
多个模块 CMakeLists、`tests/CMakeLists.txt`、`.gitignore`、`docs/adr/0158-*`、`.planning/radiometric-spectral-workbench/*`。

**业务重叠判定**：#1008 的 "radiometric" = **绝对定标 DN→Radiance→TOA/BOA**（传感器物理）；
本 track B 包 "radiometric balancing" = **镶嵌场景间相对匀色（overlap 统计 + gain/bias 归一化）**。
二者业务不同、文件不相交（本 track 不碰 `radiometric_calibration.*`）。
共享 integration files：`tests/CMakeLists.txt`（append-only 追加，冲突面最小化）、`.gitignore`（不碰）。

## 启动时 open issues（#1001–#1007）

全部属于 dataset/workflow/georef/io/agent 域（#995/#996 residual 与 R2 findings），
**无一落在 mosaic/fusion 主领域**。逐条 dedupe 结论：本 track 不实施、不修复，
不在 PR 中引用为已解决。其中与几何/裁剪相关的 #1001（io:clip CRS override）、
#1005（georef mapPick CRS transform）属于其它领域权威，记入 PARALLEL_OWNERSHIP.md。

## ISSUES.md 时效性

`ISSUES.md` = **D3 lab content expansion track 的算子缺口 backlog**（T-1..T-3 / S-1..S-2 / H-1..H-2）。
经与 master 代码比对：`rs:temporal_regularize`（T-2）、`rs:temporal_harmonic_breaks`（T-3）、
SAR/光谱多数条目已被 10.0 系列修复（见 CHANGELOG [Unreleased] 10.0 条目）。
**不作为本 track backlog**。其中与本领域唯一相关的线索：无。

## master 现有能力（mosaic / fusion 域，Phase 0 验证）

| 能力 | 位置 | 状态 |
|---|---|---|
| 遗留 kernel `Mosaic::merge`（数组粘贴、nodata、last-wins） | `src/processing/algorithms/mosaic.{h,cpp}`（67 行） | 有 |
| 生产算子 `rs:mosaic`（流式 512² 窗口、first/last-valid、CRS/像素尺寸/旋转/Y 向护栏、原子清理、cancel、200MP cap、单波段） | `src/operators/rs/rs_mosaic_operator.cpp`（457 行） | 有 |
| 遗留融合 `ImageFusion`（linear/brovey/pca/ihs/gs + 直方图匹配 + 文件级 processNativeFusion） | `src/processing/algorithms/image_fusion.{h,cpp}` | 有 |
| 现代融合 `rs::algorithms::PanSharpening`（GS/Brovey/IHS/HPF + Wald ERGAS/CC/RMSE/SSIM） | `src/processing/algorithms/pansharpening.{h,cpp}`（D14/ADR 0159, commit `89e2253b18`） | 有 |
| 融合算子 `rs:image_fusion` + `rs:fusion_*` 别名（方法：linear/brovey/pca/ihs） | `src/operators/rs/rs_image_fusion_operator.*`, `rs_fusion_aliases.*` | 有 |
| GDAL pan-sharpen 算子 | `src/operators/gdal/gdal_pansharpen_operator.*` | 有 |
| seamline / 匀色 / 融合羽化 / 金字塔融合 / quality composite / per-pixel provenance | — | **无（本 track 缺口）** |
| mosaic plan（extent/CRS/overlap inventory） | — | **无** |
| fusion quality report artifact（JSON + 失真防护） | — | **无（evaluateQuality 仅内存指标）** |

## 注册 seam（新增算子必须接线的最小面）

1. `src/operators/CMakeLists.txt`（SICNU_OPERATORS_SOURCES 追加）
2. `src/operators/rs/rs_operators_init.cpp`（REGISTER_RS_OPERATOR 宏 + factory 列表，2 行）
3. `src/agent/harness/capability_catalog.cpp`（`{ "rs:mosaic", "raster_spatial" }` 行追加）
4. `src/contracts/scientific_contract.cpp`（contract row 追加，模式同 rs:mosaic 行）
5. `tests/CMakeLists.txt`（测试 target 追加，append-only）

## 构建事实

- preset：`dev-default`（Debug、ENABLE_TESTS=ON、binaryDir `build-dev`）。
- 主仓库 build-dev 使用 **Unix Makefiles** + `/usr/sbin/c++`。
- 主机：16 cores / 64 GB RAM / 157 GB 磁盘可用。
- 资源上限（GOAL 硬约束）：`-j2` build、`-j1` test、`QT_QPA_PLATFORM=offscreen`。

## 网络事实

- 启动时 `git fetch` 出现一次 TLS `unexpected eof`（重试后成功，无引用变化）；
  `gh` API 全程可用。结论：网络抖动存在，凡 master 事实以本地 origin/* ref + gh 查询双源为准。
