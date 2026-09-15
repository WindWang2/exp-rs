# CAPABILITY_MATRIX — before / after（交付时逐行回填证据列）

| 能力 | Before（master a5b11b7f） | After（本 track 目标） | 证据（回填） |
|---|---|---|---|
| 干涉基线（B∥/B⊥）原子 | `interferometricBaseline`（sar_orbit.h，已知答案） | 保留；pair 级一致性封装于 sar_baseline | test_sar_orbit.cpp（既有）+ test_sar_baseline.cpp |
| pair 级输入真值（波长/UTC/轨道一致性） | 无（各算子各自散读元数据） | `sar_baseline`：InSarSceneTruth 校验 + `buildPairTruth` + 波长不一致 typed refusal | TEST_MATRIX 行 A-* |
| DEM/orbit 地形相位 | 无（仅低阶多项式 ramp，文档声明 NOT topo） | 几何真值链：DEM 高程 → 双星 forwardRangeDoppler → φ_topo=−4π(r1−r2)/λ → wrap；去除 + 残差；缺元数据 typed refusal | TEST_MATRIX 行 B-*（解析容差） |
| 共注册 | 全局平移 NCC（rs:sar_coregister） | 多尺度局部偏移场（格点 patch NCC + 中值滤波 + 置信掩膜 + 分块双线性 warp）；offset/confidence 产品 | TEST_MATRIX 行 C-* |
| 相干质量/掩膜 | windowCoherence（干涉图算子内） | 质量掩膜语义（相干阈值 + NaN 边界）+ 偏移场置信度 | TEST_MATRIX 行 C-mask |
| 相位解缠 | builtin quality-guided；provider≠builtin 一律 refusal | builtin 不变 + 外部可执行 provider registry（bin 发现/参数模板/超时/取消/原子清理/输出校验/typed refusal 全链） | TEST_MATRIX 行 D-*（假 provider 正/负例） |
| 多时相 pair 网络 | 无 | 时间/空间基线约束 pair graph、连通性、reference 选择、波长/元数据 fail-closed、triangle 相位闭合 QA | TEST_MATRIX 行 E-* |
| 相位闭合 | 无 | `phaseClosure` kernel（ wrapped 三角恒等 ≈0）+ 栈级 RMS | TEST_MATRIX 行 E-closure |
| 时序反演 | 无（PSI/SBAS 明确不做） | 小基线线性网络反演：per-epoch 位移 + 速度 + 残差 RMS + 时间相干；缺测 intersect/perpixel 两种语义（pattern 上限）；权重=相干 | TEST_MATRIX 行 F-* |
| 算子表面 | 18 个 rs:sar_* | +4（remove_topographic_phase / coregister_local / pair_network / network_inversion），unwrap 扩展 provider | registry + capability drift gates 全绿 |
| 能力知识 | sar.json 既有条目 | +4 条目 + algorithm_meta 文件 + 新 failure codes 入封闭词表 | test_capability_drift / test_algorithm_meta_drift |
| 文档 | sar-domain.md §8 InSAR 基础链 | 新增 §10–§14（topo phase/coreg local/provider/pair network/inversion）中文 | 文档 diff |
| 诚实边界 | 声明不做 PSI/大气/全局解缠 | 维持并写入新文档；F 以"小基线线性近似"命名，不冒充 PSI | 文档 + 命名 |
