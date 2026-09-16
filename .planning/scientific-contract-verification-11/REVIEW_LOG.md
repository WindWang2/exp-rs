# REVIEW_LOG — scientific-contract-verification-11

## Review 1 — 主 agent 全 diff self-review（Phase 5–6 期间，逐提交前）

发现并当场修复（均已入库）：
- declRe 把 `class Foo final :` 的 `final` 当类名（off-by-one 于前缀 token 提取）→ 修复 + 合成树测试覆盖。
- 注册扫描捡到文档注释里的占位 id（"my:operator"）→ first-party 前缀过滤。
- 注释中字面引用 `class IoTranslateOperator final :` 遮蔽真实类切片 → 多候选切片解析（override 优先 > header > 先见）。
- `rs:sar_displacement` + 16 个其他 live rs: 算子缺契约记录 → coverage sweep 一次性补齐。
- band_ratio 比值路径不掩 NoData（M4 抓到，silent ratio 1.0）→ bandRatioTile 掩膜重载 + 输出 NaN NoData 声明。
- determinism 计数断言放在 Catch2 sectioned case 内（每叶子组合重跑）→ 拆独立 case。

## Review 2 — 独立对抗 review（subagent #2，只读，全 diff `origin/master...HEAD`）

结论：**P0 = 0，P1 = 3，P2 = 5，P3 = 10**；verdict "…fix the records and the snapshot paths, commit the dangling M6 test repair, and this is mergeable."
确认亮点：两个 P0 host 修复最小且正确；ladder skipped 永不计为 pass；band_ratio 掩膜科学正确（bandNodata 未声明返回 quiet_NaN 不误触发；safeDiv 零分母已 NaN）；census 是投影非第二真值；mutation-kill 有真实判别力；operator_param_scanner/io_operators/cartography_operators 相对 master 无意外改动。

| ID | 级别 | 处置 | commit |
|---|---|---|---|
| F-01 | P1 | **修复**：census/scanner 路径一律 `generic_string()`；两快照再生为 0 反斜杠（平台可移植字节 gate） | 17818353 + snapshot commit |
| F-02 | P1 | **修复**：rs:sar_coregister 记录改为 amplitude→amplitude（写重采样复数 slave；reportOnly 入 note） | 17818353 |
| F-03 | P1 | **修复**：rs:classify/regress/change 记录改为 outputDomain none / operator_level cancel / typed JSON artifact note | 17818353 |
| F-04 | P2 | **修复**：spectral_band_select → outputDomain any + srf_or_center | 17818353 |
| F-05 | P2 | **修复**：library_select → outputDomain table + direct_write（subset.save 原子性未验证已注明） | 17818353 |
| F-06 | P2 | **修复**：malformed exemptions → loader error + census note + 红 test（截断 JSON） | 17818353 |
| F-07 | P2 | **修复**：5 个 cartography `bit_exact` → `bit-exact`（消除 stampDeterminismGrade/MCP get_operator_schema 抛异常） | 17818353 |
| F-08 | P2 | **修复**：M6 修复提交（north-up 拼接）；run_inv2.err 杂物删除 | 17818353 |
| F-09 | P3 | **修复**：declRe base 子句 `[^{;]+` | 17818353 |
| F-13 | P3 | **修复**：删除从未调用的 agentToolSurfaceIds 死代码 | （cross-surface patch） |
| F-14 | P3 | **修复**：--census-* 与 --out/--check 混用 → usage error | 17818353 |
| F-16 | P3 | **修复**：stamped-schema 下限按实测 19 钉住（首次误用 58 度量已纠正） | floor commit |
| F-18 | P3 | **修复**：band_ratio 输出波段声明 NaN NoData | 17818353 |
| F-10 | P3 | **Disposition: deferred**（注释内引用 override 的误选——当前树无实例；已在 scanner-scope follow-up 记录：strip comments before factsInBody） | — |
| F-11 | P3 | **Disposition: deferred**（每类多 stamp 取首个——当前树 stamp 单一；同上 follow-up） | — |
| F-12 | P3 | **Disposition: deferred**（多继承仅走 bases.front()——当前无多基算子类；BFS 扩展列入 follow-up） | — |
| F-15 | P3 | **Disposition: deferred**（census 重复全树读 + 无 memoization——已用实测 timeout 定界；single-walk 缓存列入 follow-up，PERFORMANCE.md 有记录） | — |
| F-17 | P3 | **修复**：scientific_contract.h 头注释更新（见 header commit） | final docs commit |

P1 清零证据：F-01/02/03 修复后 Run1+Run2 全部 10 套件连续两遍全绿（断言数逐套一致）。

## 已知 pre-existing（非本 track 引入，处置=记录+分类）

- ladder L2：drift_projection_10（spectral_unmixing 等 sidecar 缺参数行）、contract_platform_9（3 × duplicate capability node）、command_contract_9（cartography.repair 等 help 缺页）、diagnostics_contract_9（CATEGORICAL_MISMATCH 无页）、contract_projection_9（matched_filter unresolved）—— 五项的 owning 文件全部不在本 diff。
- fuzz_ipc：Windows named-pipe 仿真上 >50 分钟未完成（单跑实证）→ host limitation，ladder 记 timeout（never pass）。
- open issues #1001–#1007：fail-open 类，他人 own 区，EVIDENCE/FAILURE_MATRIX 已记录。
