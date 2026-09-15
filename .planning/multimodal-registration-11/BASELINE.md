# BASELINE — multimodal-registration-11

Phase 0 只读审计（2026-09-15，主仓库 `/home/kevin/projects/rs-studio/main`）。所有断言为启动时实测。

## origin 事实（启动时刷新）

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  （`fix: fail-closed fixes for review issues #994–#999 (#1000)`）
- 注意：GOAL prompt 生成时的 `ebcafb4d02` 已过期。**PR #991（D18 workbench）与 #992（D19 foundry/benchmark）均已合并进 master**（`c5d4aafe8e`、`1cea98921b`），不再是并发约束。
- `git fetch origin --prune` 首次尝试 TLS EOF 失败，重试成功（网络抖动，已记录）。

## Open PRs（启动时）

| PR | branch | 状态 | 与本 track 文件级交集 |
|---|---|---|---|
| #1008 `feat(spectral): Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench` | `zcode/radiometric-spectral-workbench` | open | 无业务交集。仅共享 integration files：`src/agent/CMakeLists.txt`、`src/analysis/CMakeLists.txt`、`src/app/CMakeLists.txt`、`src/core/CMakeLists.txt`、`tests/CMakeLists.txt`、`.gitignore`、`src/agent/spatial_tools/spatial_tool.cpp`（它注册 spectral 工具；本 track 在同一函数注册 geometric 工具 → append-only 冲突面，rebase 时逐行处理） |

## Open issues（启动时，全部 dedupe 结论）

| Issue | 域 | 归属判定 |
|---|---|---|
| #1001 `io:clip uses srcCrsOverride as targetCrs` P1 | io operators | OUT_OF_SCOPE（io 域，非本 track scope；PR_BODY 顶部转述） |
| #1002 `makeRegistryNodeExecutor reports success without verifying artifact` P1 | workflow | OUT_OF_SCOPE |
| #1003 `joinFeaturesBySampleId treats JSON-null required columns as present` P1 | dataset | OUT_OF_SCOPE |
| #1004 `dataset:qa identity Passes uniqueness when scan_capped` P1 | dataset/agent | OUT_OF_SCOPE |
| #1005 `mapPickToLayerCrs returns untransformed canvas point when CRS transform throws` **P1 critical** | **georef（本 track primary scope）** | **IN SCOPE**：本 track 修复（fail-closed），不关闭 issue（无权限），PR_BODY 注明 addresses #1005 |
| #1006 `PipelineRunCoordinator soft-defaults to syntheticExecute` P2 | workflow | OUT_OF_SCOPE |
| #1007 `dataset:qa never audits CRS` P2 | dataset | OUT_OF_SCOPE |

## ISSUES.md / CHANGELOG 复核

- `ISSUES.md` 是旧 D3 lab-content backlog（T-1..T-3, S-1..S-2, H-1..H-3, C-1..C-2）。逐条核对：T-1/T-2/T-3/C-2 已被 10.0 temporal 平台修复（CHANGELOG [Unreleased] Temporal Platform 10.0 记录 `rs:temporal_regularize`、`rs:temporal_harmonic_breaks`、`rs:temporal_extract_regions`、monitor scenes seam）。S-1/S-2、H-1..H-3、C-1 属 SAR/光谱/制图域，不在本 track scope。**无一条需要本 track 实施。**
- `CHANGELOG.md` 头部确认 master 已含 Data Fabric 10.0、Temporal 10.0 等；geometric 域最新条目 = D14（ADR 0159）。

## 现有 geometric 能力（Subagent #1 深度审计结论摘要，全部带 file:line 证据）

**已有（D14 / Phase 11.x，master @ a5b11b7f10）：**
- `src/processing/algorithms/feature_matcher.{h,cpp}`：单 octave 网格关键点 + SIFT-like 32 维梯度直方图 / ORB-like patch 描述子、Lowe ratio、确定性 RANSAC 单应（mt19937(42)、自适应迭代、LS refit）。
- `src/processing/algorithms/geometric_transform.{h,cpp}`：Translation/Rigid/Similarity/Affine/Poly2/Poly3/Projective 闭式拟合、正反 RMSE、κ<1e14 门限。**无模型选择/过拟合拒绝。**
- `src/processing/algorithms/tps_interpolator.{h,cpp}`、`resampler.{h,cpp}`（NN/bilinear/cubic/Lanczos + reverse-map warp + NoData 权重）、`pansharpening.{h,cpp}`（GS/Brovey/IHS/HPF + Wald 指标）。
- `src/processing/algorithms/gcp_manager.{h,cpp}`：GCP CRUD/CSV/JSON、残差/RMSE、Clark-Evans 空间分布指标、Delaunay、凸包。
- `src/analysis/georeferencing/`：QGIS vendored transformer（Linear/Helmert/GDAL-TPS/Poly/Projective）+ SICNU RPC transformer（GDAL RPC + DEM + 常数 z-offset + **仅常数 lon/lat 中位数偏置** GCP 精化，≥3 GCP，median-improvement gate）。
- `src/app/georeferencer/`：双画布 shell、GCP 表/残差图/RMS 散点、OpenCV SIFT 对话、NCC template matcher、Task Center dispatch（`module:georef:sift/template_match/warp`）、session/warp snapshot。
- `src/app/workbench/georef_dual_window.{h,cpp}`：双窗联动 workbench（D18 已 mount）。
- `src/agent/tools/geometric_tool.{h,cpp}`：`spatial:geometric_registration`（audit_residuals/recommend_model/inspect_misalignment），**但未注册进 `SpatialToolRegistry::registerBuiltinTools()`（spatial_tool.cpp:220-282），目录中不可见。**
- `rs:sar_coregister`（仅平移）、`rs:align`（格网 snap）、`rs:modis_georeference`。**无 `rs:` 配准/匹配/warp 交互式算子暴露。**
- 测试：test_feature_matcher（独立真值单应）、test_geometric_transform（解析真值）、test_rpc_gcp_refine（手算预测）、test_rpc_golden（哈希回归，非独立）、test_d14_geometric_registration_e2e（解析相似变换 oracle）、warper_test_helpers.h（合成 RPC/DEM fixtures）。

**确认缺失（grep 0 hit）：**
- phase correlation、mutual information、图像金字塔/多尺度、coarse-to-fine、CE90、residual vector field、local confidence、stack registration / pair graph / global adjustment / loop closure、跨模态（SAR-like）描述子。

## 缺口 → Work package 映射（rescope 决策见 DECISIONS.md）

| Pkg | 状态 | 本 track 动作 |
|---|---|---|
| A multimodal descriptors | MISSING | 新建 `registration/` 模块：phase/gradient/RANK 描述子 + MI 度量 |
| B coarse-to-fine | PARTIAL | 金字塔 + phase corr + 覆盖约束 + 复用 RANSAC |
| C model selection | PARTIAL | 证据驱动选择器（holdout CV + 改进门限 + κ 门限） |
| D RPC/DEM refine | PARTIAL | bias 模型升级（constant→affine，CV 选择）+ 高度敏感性 |
| E stack registration | MISSING | reference scene / pair graph / 全局平移-仿射 adjustment / 闭环残差 |
| F quality products | PARTIAL | CE90/residual field/local confidence/quality report JSON |
| G UI/agent tools | PARTIAL | 注册既有 tool + 新 action + `rs:` 算子 + 结构化 explain；**修复 #1005** |
| H synthetic warps | PARTIAL | 图像级 warp 生成器 + SAR-like 模拟 fixture |
