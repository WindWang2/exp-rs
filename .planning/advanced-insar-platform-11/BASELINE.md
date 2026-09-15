# BASELINE — 启动时最新态审计（2026-09-15，Phase 0 原始记录）

## Git / GitHub 事实（启动时刷新，全部已验证）

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  （"fix: fail-closed fixes for review issues #994–#999 (#1000)"）。
  **Prompt 快照中的 `ebcafb4d` 已过期；以本 SHA 为本 track 基线。**
- 最近 20 commit 摘要：
  - `a5b11b7f` fix: fail-closed fixes for review issues #994–#999 (#1000)
  - `1cea9892` Merge branch 'grok/dataset-foundry-benchmark-d19' (#992)
  - `c5d4aafe` D18: Unified Mission Workbench — MissionContext + D14/D15/D17 mounts (#991)
  - `77e178ac` fix(ci): macOS/Windows compile errors in d17 and Win32 paths (#993)
  - `08264801..44617ff9` D19 dataset foundry & benchmark 系列
  - `ebcafb4d` fix(ci): OSR WKT import in test_io_operators (#990)
  - `b91753ff/f368b9fd/64418b72` merge zcode classification-change-studio / workflow-pipeline-designer / geometric-registration-workbench
  - `e8c4bf43` D16 Temporal Phenology Timeline Studio (#986)
- Remote branches（按提交时间）：`origin/zcode/radiometric-spectral-workbench`（唯一非 HEAD remote 分支）。
- **Open PR（1 个）**：#1008 `feat(spectral): Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench`
  - head `zcode/radiometric-spectral-workbench`，base master，mergeable=**CONFLICTING**（与 master 冲突，非本 track 造成）。
  - changed files（`gh pr diff 1008 --name-only`，共 39 个）：spectral/radiometric 业务
    （`src/processing/algorithms/spectral_*`、`src/analysis/{atmospheric,hyperspectral}`、
    `src/core/{radiometric_state,spectral_library}`、`src/app/widgets/spectral_*`、
    `src/agent/spatial_tools/spectral_*`、docs/adr/0158、其 `.planning/` 与 8 个 spectral 测试）。
  - **与共享集成文件交集**：`.gitignore`、`src/agent/CMakeLists.txt`、`src/analysis/CMakeLists.txt`、
    `src/app/CMakeLists.txt`、`src/core/CMakeLists.txt`、`src/processing/CMakeLists.txt`（未列入其 diff——
    它改的是 algorithms 根下新文件，需 rebase 后复核）、`tests/CMakeLists.txt`。
  - **与本 track 主写域（`src/processing/algorithms/sar/**`、`src/operators/rs/*sar*`、
    `data/agent/capabilities/sar.json`、`docs/processing/sar-domain.md`、`tests/*sar*`）零文件交集。**
- **Prompt 快照中的 #991/#992 均已合并进 master**（c5d4aafe、1cea9892）→ 按 Phase 0 规则 3，
  以新 origin/master 重新审计，D18/D19 产物成为 master 事实的一部分（不再是并发风险面）。
- **Open issues（7 个）**：#1001 io:clip CRS 误用、#1002 workflow registry fail-open、
  #1003 dataset join null 列、#1004 dataset:qa scan_capped、#1005 georef CRS transform 吞异常、
  #1006 PipelineRunCoordinator syntheticExecute 残留、#1007 dataset:qa CRS 缺审计。
  **逐条 dedupe 结论：全部属于 io/workflow/dataset/georef/agent 领域，无一落在 SAR/InSAR
  主写域；无"已被本 track 范围内代码修复但未关"的条目。** 全部记 OUT_OF_SCOPE，不在本 track 实施。
- `ISSUES.md`（旧 D3 backlog）核验：S-1（sar_change 严格双时相/argmax_date 索引）与 S-2
  （无极化分解）仍是对话线索，但**不属于本 track 的 InSAR mission**（多时相变化检测日历语义 ≠
  干涉 pair 网络）；记录 OUT_OF_SCOPE，不据此开工。其余 T-*/H-*/C-* 已被 10.0 各 PR 修复或
  与 SAR 无关。

## 现有 SAR/InSAR 能力证据（文件:行号级）

- `src/processing/algorithms/sar/sar_insar.h:1-38`（诚实范围声明）：基础干涉链 = 复数 SLC pair
  preflight、interferogram/coherence、Goldstein-Werner 滤波、quality-guided 参考解缠、
  flat-earth 多项式 ramp 去除、LOS 位移转换。**明确不做**：完整共注册（仅全局平移）、
  DEM/orbit 地形相位、大气改正、PSI/SBAS。→ 本 track package B/C/D/E/F 的缺口声明与代码一致。
- `sar_insar.h:164-166` qualityGuidedUnwrap（builtin）；`rs_sar_unwrap_operator.cpp:115-119`
  `provider != "builtin"` → UNWRAP_PROVIDER_UNAVAILABLE（**外部 provider seam 目前是纯 typed
  refusal，无实现**）→ package D 的真实缺口。
- `sar_insar.h:193-201` coregistrationShift（全局平移 NCC）+ shiftComplexBilinear；
  `rs_sar_coregister_operator.h:17-21` 诚实声明"translation only"→ package C 的真实缺口
  （局部偏移场/多尺度）。
- `sar_orbit.h`：Wgs84 ECEF↔geodetic、OrbitSegment/parseOrbitStates（SICNU_SAR_ORBIT_STATES
  契约）、Hermite interpolateState、geolocateZeroDoppler、forwardRangeDoppler、
  incidenceAngleDeg、interferometricBaseline（B∥/B⊥/|Δr|，D-011）→ package A 的既有原子，
  需要统一为 pair 级版本化输入真值（波长/时间/几何一致性校验）。
- `sar_complex.h`：CFloat32 原生流（ComplexBandTileStream 双流 lockstep）、通道声明契约、
  writeComplexTile → 干涉处理 IO 基座已就绪。
- `sar_temporal_events.h:41-44` parseAcquisitionUtc / kAcquisitionUtcKey =
  SICNU_SAR_ACQUISITION_UTC → 时间真值既有契约。
- 测试基座：Catch2 v3；kernel 级纯内存 fixture（test_sar_insar.cpp）；operator 级 GTiff fixture
  （test_sar_operators.cpp writeRaster，EPSG:32648）；test_sar_platform10.cpp 为 registry+run
  集成模式。
- Surface/drift gates：`data/agent/capabilities/sar.json`（test_capability_drift.cpp:206 强制
  每个注册算子有条目）；`data/processing/algorithm_meta/capability/rs-sar-*.json`
  （test_algorithm_meta_drift）；`data/contracts/contract_graph.snap.json`
  （`contract_inventory` 工具再生，test_contract_platform_9 字节比对）；
  `src/agent/harness/capability_catalog.cpp:35-43` kFailureModeCodes 封闭词表；
  `src/agent/harness/harness_error.cpp` 重试分类表。

## 结论（rescope 判定）

Prompt 中的 package A–H 与 master 现状核对后**全部仍为真实缺口**（无已被覆盖项）；
rescope 调整仅一处：package A 不重写 interferometricBaseline/轨道插值（已存在且已知答案测试），
而是新增 pair 级版本化输入真值层（sar_baseline），消费既有 authority 原子。详见 DECISIONS.md。
