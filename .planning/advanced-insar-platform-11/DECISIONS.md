# DECISIONS — Advanced InSAR Platform 11.0

格式：D-xx | 日期 | 决策 | 候选与理由 | 影响

## D-001 | 2026-09-15 | package A 以新增 `sar_baseline` 模块承载 pair 级真值，不改动 sar_orbit/sar_insar 既有 API
- 候选 A：在 sar_orbit.h 内扩展 pair 真值结构。拒绝：该文件是 Scientific Algorithms 7.0 的
  稳定 authority，被 geocoding/terrain 等多域消费；改动 API 面会放大冲突面。
- 候选 B（采纳）：新增 `sar_baseline.h/.cpp`，纯消费既有原子（OrbitSegment/
  interferometricBaseline/parseAcquisitionUtc），additive。
- 影响：既有测试零改动；新模块独立 known-answer 测试。

## D-002 | 2026-09-15 | 地形相位采用严格几何链（双星 forwardRangeDoppler），不用 B⊥ 近似
- 候选 A（采纳）：逐像元 DEM 高程 → 双星 forwardRangeDoppler → r1/r2 →
  φ_topo = −(4π/λ)(r1−r2)，wrap 到 (−π,π]；去除 = wrap(φ_ifg − φ_topo)。
  精确、无近似假设、直接复用零多普勒 authority；代价是每像元两次牛顿/二分（可流式分块）。
- 候选 B（拒绝，文档记录）：φ_topo ≈ (4π/λ)·B⊥·h/(r·sinθ) —— 需逐像元 θ 与基线符号约定，
  且在起伏地形/大基线下失真；作为文档对照说明保留。
- 符号约定（写入头文件与文档）：φ = arg(s_master·conj(s_slave)) = −(4π/λ)(r_master − r_slave)；
  正 d_los = 朝向传感器（沿用 sar_insar.h 现行 d_los = −λφ/(4π)）。

## D-003 | 2026-09-15 | 共注册局部偏移场为"平移场模型"（piecewise translation），不做仿射/range-Doppler warp
- 理由：诚实范围（sar_insar.h 已声明无仿射 warp）；局部平移场已覆盖 InSAR 主流失配
  （轨道斜坡表现为缓变平移场）；仿射 warp 属几何重采样域（D14 geometric workbench 所在）。
- 产品语义：offset 场（2 波段 dx,dy，像元级双线性插值）+ confidence 波段；conf 不足处
  回退全局平移（显式记录 fallbackCount），不是 NaN 静默混合。

## D-004 | 2026-09-15 | 外部解缠 provider = 通用可执行进程适配（无 SNAPHU 代码/依赖），SNAPHU 作为命名预设
- 候选 A：编译期集成 SNAPHU。拒绝：新重量级依赖 + 许可/跨平台问题，违反 envelope。
- 候选 B（采纳）：`sar_unwrap_provider` registry：provider=外部名 → bin 发现
  （param providerBin > env `SICNU_SAR_UNWRAP_<NAME>_BIN`）→ raw float32 中转文件
  （`.tmp~` 原子、QTemporaryDir/workDir、退出即清理）→ args 模板占位符
  {input}{output}{width}{height} → QProcess 执行（超时/取消 kill/stderr 捕获）→
  输出校验（尺寸/有限样本数 ≥ 输入有效数）。缺 bin → UNWRAP_PROVIDER_UNAVAILABLE；
  非零退出 → UNWRAP_PROVIDER_FAILED；超时 → UNWRAP_PROVIDER_TIMEOUT；
  输出不合格 → UNWRAP_PROVIDER_INVALID_OUTPUT。snaphu 预设文档化其命令行形态。
- 影响：零依赖增量；离线/无工具环境为 typed refusal 而非降级静默。

## D-005 | 2026-09-15 | pair 网络 fail-closed 语义分级
- 元数据缺失/波长不一致/轨道无效 = 一律 typed refusal（Oracle 2）。
- 约束下图断连 = 默认 refusal（`PAIR_GRAPH_DISCONNECTED`）；`allowDisconnected=true`
  时返回带分量标注的结果（诚实 QA 用途），结果中 components 字段显式列出，绝不静默。
- reference 默认最早获取（explicit 参数可覆盖）；策略 all_pairs / consecutive 起步。

## D-006 | 2026-09-15 | 时序反演命名与范围
- 算子名 `rs:sar_network_inversion`；文档命名"小基线线性网络反演（SBAS-style linear）"，
  显式声明非 PSI（无大气分离/无 PS 选择）。候选名 rs:sar_sbas 被拒（冒充完整 SBAS 语义）。
- 缺测语义两级：`maskStrategy=intersect`（默认，像元被任何 pair 缺测即剔除，文档声明）；
  `perpixel`（NaN-pattern 分组求解，distinct pattern > 128 → typed refusal
  `NETWORK_INVERSION_PATTERN_BLOWUP`，建议 intersect）。
- 权重 = coherence²（缺相干输入 → 均匀权重 + 结果标注 unweighted=true）。

## D-007 | 2026-09-15 | 错误码并入既有封闭词表而非新建词表
- 新域码：TOPO_PHASE_METADATA_MISSING、WAVELENGTH_MISMATCH、ORBIT_EPOCH_MISMATCH、
  PAIR_GRAPH_DISCONNECTED、UNWRAP_PROVIDER_FAILED、UNWRAP_PROVIDER_TIMEOUT、
  UNWRAP_PROVIDER_INVALID_OUTPUT、NETWORK_INVERSION_PATTERN_BLOWUP、
  DEM_GRID_UNSUPPORTED（DEM 与干涉网格 CRS 不兼容）。
- 全部追加进 `capability_catalog.cpp kFailureModeCodes[]` + `harness_error.cpp` 分类；
  沿用自由前缀约定，不建第二词表。

## D-008 | 2026-09-15 | 保守命名与最小 additive 表面
- 新算子：`rs:sar_remove_topographic_phase`、`rs:sar_coregister_local`、
  `rs:sar_pair_network`、`rs:sar_network_inversion`。既有算子签名只做向后兼容扩展
  （unwrap 新增可选参数，默认行为不变）。

## D-009 | 2026-09-15 | 相位闭合 QA 以 kernel + pair_network 集成起步
- 独立算子 `rs:sar_phase_closure` 暂缓（若有预算余量在 Phase 6 前评估）；三角闭合统计
  作为 pair_network 输出的可选 QA 字段 + kernel 级测试，避免 surface 膨胀。
