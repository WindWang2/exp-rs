# CURRENT_ARCHITECTURE — scientific-contract-verification-11（baseline `a5b11b7f` 现状 authority/seam 图）

## Authority 地图（谁拥有哪个事实）

| 事实 | 权威 | 位置 | 消费者 |
|---|---|---|---|
| 算子存在性/参数 schema | live registry（`RSOperatorRegistry`） | `src/operators/framework/rs_operator_registry.h` + 各 `*_operators_init.cpp` | GUI/CLI/MCP/agent/schema listing |
| determinism grade（已发布者） | schema root `determinismGrade`（= `determinismGrade()` 虚函数） | `rs_schema.cpp` `stampDeterminismGrade`；override 54 处 | drift gate、agent |
| determinism grade（知识层） | capability sidecar `capability.determinism.{grade,stochastic}` | `data/processing/algorithm_meta/capability/rs-*.json`（125 个） | agent harness、capability_pages |
| 科学语义（domain/NoData/time/seed/cancel/atomic/provenance） | scientific contract registry | `src/contracts/scientific_contract.{h,cpp}`（115 个 rs:） | tests、contract graph |
| 运行时并行确定性 | `RSOperator::determinism()`（默认 BitExact，7 处 override） | `rs_operator.h` L170 | 执行规划（ADR 0124） |
| 模型推理 provenance | `exp-rs-prov/1` sidecar + verifier | `src/operators/runtime/provenance_verify.*` | tile inference |
| 契约图快照 | `data/contracts/contract_graph.snap.json`（字节 gate） | `contract_inventory` 工具再生 | `test_contract_platform_9` |
| verification lanes | `scripts/verification_ladder.py`（L0–L8） | lane 硬编码表 | local runner |

## 已证实的缺口（11.0 的作业面）

1. **G1 determinism 发布缺口**：`determinismGrade()` override 54 处 vs live 算子 ~140（115 rs: + 14 io: + 4 otb: + 6 opencv: + 5 cartography:）；未覆盖者 schema 无 stamp，drift gate 显式不绑定（10.0 EVIDENCE："unproven, not tolerance"; ~80+ sidecar claims 无代码侧发布）。`determinism()` 默认 BitExact 与 `determinismGrade()` 默认 "tolerance" 在基类互相矛盾（rs_operator.h L145 vs L172）——未覆盖算子的两个权威不同值。
2. **G2 执行证据缺口**：sidecar 的 bit_exact 声明无系统化 replay 证据（10.0 仅 band_ratio/kmeans 两条 lane）。
3. **G3 非 rs: 契约缺口**：io:/otb:/opencv:/cartography: 算子无 scientific contract 记录、无 exemption 机制；census 不可见。
4. **G4 metamorphic 稀薄**：仅 1 条不变量（NDVI scale）。
5. **G5 独立数值 oracle 缺口**：known-answer 有闭式断言但无独立高精度（long double）/独立推导 lane。
6. **G6 failure/cancel/atomic 行为与 contract 声明未闭环**：contract 声明 refusalCodes/atomicPublication/cancellationGranularity，但没有系统性 negative lane 验证"声明的行为真的发生"。
7. **G7 ladder 未覆盖 11 系列测试**，且 `find_binary` 不识别 Windows `.exe`；无 host capability 显式声明。
8. **G8 open issues #1001–#1007 全部是 fail-open/静默语义类**（OUT_OF_SCOPE 点修，见 EVIDENCE）——反向印证 G6 机制价值。

## 11.0 新增（本 track own）

```
src/contracts/determinism_census.{h,cpp}   # A/B: live census + 源扫描 + canonical JSON (exp.determinism_census.v1)
data/contracts/determinism_census.snap.json# census 快照（字节 gate）
data/contracts/determinism_exemptions.json # 非 rs: 前缀契约 exemption + 无 replay 配方的诚实豁免表
src/contracts/tool/…                       # census 快照再生/校验（复用 contract_inventory 模式）
tests/test_contract_census_11.cpp          # A/B gates（覆盖性、override 诚实性、快照字节）
tests/test_contract_determinism_11.cpp     # B: replay 语料 + 执行证据 + sidecar/stamp/census 三方一致
tests/test_verification_metamorphic_11.cpp # C: 族不变量 + NoData/CRS/time
tests/test_mutation_kill_11.cpp            # C: 变异注入必须被 oracle 抓到（oracle 有效性反证）
tests/test_verification_numeric_reference_11.cpp # D: 独立闭式/高精度 oracle
tests/test_verification_failure_11.cpp     # E: corrupt/cancel/read-only/半成品
tests/test_contract_cross_surface_11.cpp   # F: help↔agent↔sidecar↔graph↔registry 六面
scripts/verification_ladder.py             # G: 11 lanes + Windows exe 解析 + capability 声明
docs/verification/DETERMINISM_CENSUS_11.md # 文档
docs/verification/READINESS.{json,md}      # H: 机器可读 readiness（collect_readiness.py 再生）
```

## Reuse-not-duplicate 纪律

- census 从 `RSOperatorRegistry`/`scientificContracts()`/sidecar JSON **读取投影**，不建立第二真值（DECISIONS D-3）。
- replay/metamorphic 复用 `RsSyntheticRasterBuilder`、`compareRastersBitExact`、`RSOperatorContext`、`RSOperatorRegistry::create`。
- ladder 扩展保持 `exp.verification.ladder.v1` additive。
- 测试命名遵循 `tests/*contract*` / `tests/*verification*` write-scope glob。
