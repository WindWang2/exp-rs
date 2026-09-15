# CURRENT_ARCHITECTURE — InSAR 领域现状 authority / seam 图（Phase 0）

## Authority（唯一真值，本 track 复用不复制）

| 真值 | Authority | 说明 |
|---|---|---|
| 轨道状态/时间→位置 | `sicnu::sar::OrbitSegment` + `parseOrbitStates` + `interpolateState`（sar_orbit.h） | SICNU_SAR_ORBIT_STATES 契约，UTC 秒 |
| 零多普勒几何 | `geolocateZeroDoppler` / `forwardRangeDoppler` / `incidenceAngleDeg`（sar_orbit.h） | Newton/bisection，已知答案测试于 test_sar_orbit.cpp |
| 椭球 | `Wgs84`（sar_orbit.h） | ECEF↔geodetic 唯一入口 |
| 干涉基线 | `interferometricBaseline`（sar_orbit.h:128） | B∥/B⊥/|Δr|，单位 LOS 向量契约 |
| 复数 SLC IO | `ComplexBandTileStream` / `writeComplexTile` / 通道契约（sar_complex.h） | CFloat32 原生窗口，NaN 归一 |
| 干涉图/相干/滤波/解缠/ramp/位移核 | sar_insar.h 全族 | (−π,π] rad、[0,1] 相干、NaN 语义 |
| 获取时间 | `parseAcquisitionUtc` + `SICNU_SAR_ACQUISITION_UTC`（sar_temporal_events.h/sar_metadata.h） | ISO 8601 UTC |
| 波长 | 元数据键 `SICNU_SAR_WAVELENGTH_UM`（rs_sar_displacement_operator 现行约定） | µm；缺省 → typed refusal |
| 网格比较 | `sicnu::data::compareGrids`（GRID_MISMATCH 权威） | 干涉 same-grid 契约 D-005 |
| 算子错误 | `RSOperatorError(ErrorCode, "DOMAIN_CODE: msg")` + `kFailureModeCodes`（capability_catalog.cpp:35） | 域码自由前缀 + 封闭词表 |
| 算子注册 | `REGISTER_RS_OPERATOR`（rs_operator_registry.h:80） | 静态注册器 |
| 原子写 | 同目录 `.tmp~` + rename；`GdalStreamingOutput::closeWithError/abandon` | repo 惯例 |
| contract 快照 | `contract_inventory` 工具 → `data/contracts/contract_graph.snap.json` | 字节比对 gate |

## 现有 seams（本 track 的挂点）

1. `rs:sar_unwrap` 的 `provider` 参数 —— 目前非 builtin 一律 refusal；package D 在此缝隙
   实现真实的外部进程 provider registry，builtin 语义不变。
2. `rs:sar_coregister` 的全局平移模型 —— package C 在其旁新增局部偏移场模块（不改旧语义，
   旧算子保持全局模型；`rs:sar_coregister_local` 为 additive 新面）。
3. `rs:sar_interferogram` 的 flattenRamp（低阶多项式）—— 文档已声明"NOT topographic phase
   removal"；package B 提供真正的 DEM/orbit 链，二者并存、语义分开。
4. `rs:sar_temporal_events` 的获取时间契约 —— package E 复用为 pair 时间真值。

## 缺口 → 新模块映射（全部 additive）

| Package | 新模块（src/processing/algorithms/sar/） | 新 operator |
|---|---|---|
| A | `sar_baseline.h/.cpp`（pair 级版本化几何真值：波长/UTC/轨道一致性 + pair baseline） | —（被 B/E 消费） |
| B | `sar_topographic_phase.h/.cpp` | `rs:sar_remove_topographic_phase` |
| C | `sar_coregistration.h/.cpp`（多尺度局部偏移场 + warp + 质量掩膜） | `rs:sar_coregister_local` |
| D | `sar_unwrap_provider.h/.cpp`（外部 provider registry/进程适配） | 扩展 `rs:sar_unwrap` |
| E | `sar_pair_network.h/.cpp` + `sar_phase_closure.h/.cpp` | `rs:sar_pair_network` |
| F | `sar_network_inversion.h/.cpp` | `rs:sar_network_inversion` |
| G/H | capability/meta/contract/docs/tests 同步 | — |

## 显式不做（诚实边界，写入文档）

- 大气改正（APS 分离）——PSI 完整实现的一部分，本 track 不做；
- PSI/PS 选择、去相关掩膜的统计模型；
- 分支切断/MCF 全局最优解缠（builtin 保持 quality-guided 参考；外部 provider 负责更强语义）；
- range-Doppler 精配准（影像几何 warp）；本 track 偏移场为平移场模型并诚实声明。
