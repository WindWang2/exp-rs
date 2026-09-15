# TEST_MATRIX — scientific-contract-verification-11

每项能力 → 独立 oracle → 命令 → exit → evidence。持续追加；最终每行须有两次连续运行记录。

| # | 能力 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|---|
| T-1 | contract census 覆盖 live registry 全前缀 | census vs live registry 双向 diff（实现内建） | `ctest -R test_contract_census_11` | | |
| T-2 | determinism 无静默兜底 | census 声明来源 ⊆ {explicit, exempted} | 同上 | | |
| T-3 | determinism 执行证据 | 同一输入跑两遍 byte/数值比对 | 同上 | | |
| T-4 | metamorphic：band-scale/offset | 测试内独立闭式推导 | `ctest -R test_metamorphic_oracle_11` | | |
| T-5 | metamorphic：band-reorder | 通道置换等价（测试内置换矩阵） | 同上 | | |
| T-6 | metamorphic：warp 恒等/平移 | 恒等 transform byte 比较 | 同上 | | |
| T-7 | metamorphic：zonal 平移不变 | 区域平移后统计量相等（闭式） | 同上 | | |
| T-8 | metamorphic：NoData 单调传播 | NoData 输入→排除/NoData 输出 | 同上 | | |
| T-9 | metamorphic：temporal 平移不变 | 日期平移下统计不变（闭式） | 同上 | | |
| T-10 | mutation kill：注入变异必须被抓 | 6+ 变异样本 × 对应不变量 | `ctest -R test_mutation_kill_11` | | |
| T-11 | numeric reference：光谱指数 | long double 独立公式 | `ctest -R test_numeric_reference_11` | | |
| T-12 | numeric reference：辐射定标链 | 解析 radiance/TOA/bt | 同上 | | |
| T-13 | numeric reference：Welford/分位数 | 双通道独立算法 | 同上 | | |
| T-14 | numeric reference：重采样/聚合 | 解析情形（常数/线性场） | 同上 | | |
| T-15 | failure：corrupt input→typed refusal，无半成品 | 文件系统状态断言 | `ctest -R test_failure_contract_11` | | |
| T-16 | failure：cancel→typed cancel，无 partial publish | 同上 | 同上 | | |
| T-17 | failure：read-only 源/输出目录 | 同上 | 同上 | | |
| T-18 | cross-surface：help↔registry | help json id 可解析 | `ctest -R test_cross_surface_drift_11` | | |
| T-19 | cross-surface：agent knowledge↔sidecar | 双向 diff | 同上 | | |
| T-20 | contract graph 快照 | 字节 gate | `ctest -R test_contract_platform_9`（既有）+ 新 | | |
| T-21 | 既有 10.0 gates 不回归 | — | `ctest -R "test_scientific_contract_10|test_drift_projection_10|test_science_verification_10"` | | |
| T-22 | ladder L0–L5 capability-aware | ladder JSON schema + exit | `python scripts/verification_ladder.py ...` | | |
| T-23 | readiness 报告再生成 | collect_readiness exit + 字段断言 | `python scripts/collect_readiness.py ...` | | |
