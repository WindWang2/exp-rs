# CAPABILITY_MATRIX — scientific-contract-verification-11

before = master `a5b11b7f`；after = 本 track 分支（构建/测试验证后定稿）。

| 能力 | before | after | 证据 |
|---|---|---|---|
| rs: scientific contracts | 115/115（test 强制） | 115/115（不变） | test_scientific_contract_10 |
| io: scientific contracts | 0/14（体系外） | 14/14 records（ioFamily） | test_contract_census_11 known answers |
| cartography: contracts | 0/5 | 5/5 records | 同上 |
| gdal:/otb:/opencv: 覆盖 | 无记录 | 15 条 reviewed exemptions（contract_or_exemption gate 强制） | data/contracts/contract_exemptions.json + census gate |
| determinism census | 无（10.0 显式记 ~80+ unproven） | 全算子 source-grounded census + snapshot 字节 gate | test_contract_census_11 |
| determinism 执行证据 | 2 条 lane（band_ratio/kmeans） | 10-recipe 跨家族 replay corpus + triple-publication 绑定 | test_contract_determinism_11 |
| metamorphic 不变量 | 1 条（NDVI ramp scale） | 6 条 + 敏感度对照（random field/band-reorder/geolocation/NoData/identity-CRS/mosaic-order） | test_verification_metamorphic_11 |
| 独立数值 oracle | 无 high-precision lane | long-double NDVI/SAVI + analytic translate/clip/threshold | test_verification_numeric_reference_11 |
| oracle 有效性 | 未证明 | 10 个 mutation samples 全部被抓 | test_mutation_kill_11 |
| failure/cancel/atomic 行为验证 | io_atomic_failures（geospatial core 层） | operator seam 层 5 类负路径 + 无半成品断言 | test_verification_failure_11 |
| cross-surface | schema↔sidecar、LabSpec↔registry（10.0） | + help↔registry、agent↔registry、graph-snapshot↔live | test_contract_cross_surface_11 |
| ladder（本机） | find_binary 不识别 .exe；POSIX 项在 Windows 误报 | .exe 解析 + host capabilities + per-item requires + 11 系列 7 个 suite | scripts/verification_ladder.py |
| readiness | READINESS.v1（10.0 记 skipped=3/not_built=16） | +7 个 11.0 capability 行 + Windows exe 解析 | docs/verification/READINESS.*（构建后再生） |

## 明确 not-supported / degraded（诚实清单）

- otb:/opencv: 不做 scientific contract 记录（exemption：上游数值核心）——这是设计决策，非缺陷。
- metamorphic time 轴不变量（temporal 族）：未在本 track 实现——temporal stack fixture 成本高于本 track 边界；登记 follow-up（TEST_MATRIX T-9 降级为 not-implemented-with-reason）。
- io:reproject/warp 的"平行执行 tolerance"claim 不在本 replay 范围（replay 只证 serial byte-identity，ADR 0124 serial anchor）。
- census 的 replay 证据只覆盖 corpus 算子；其余算子的 coverage 状态由 census 行如实记录。
