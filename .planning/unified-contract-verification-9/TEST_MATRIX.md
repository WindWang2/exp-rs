# TEST_MATRIX — Contract Platform 9.0

Verification standard: **old code must fail, new code must pass** for every
guard; mutation evidence is named per row. Statuses:
`pass / fail / skip / not-built / not-run / unsupported` — never aggregated
into green.

| # | Suite | Level | Guards (issue class) | Red evidence |
|---|-------|-------|----------------------|--------------|
| 1 | `test_contract_platform_9` | L2 | graph integrity: dangling refs on live tree; duplicate-node detection (unit-provable via the addNode rejection path); snapshot byte-compare | synthetic-graph duplicate proof via the addNode API; snapshot failure names the regeneration command |
| 2 | `test_contract_projection_9` | L2 | impl-reads ⊆ schema (#872/#879/#880); dead schema params; descriptor round-trip; listSchemas ↔ schema(); MCP tool schema ⊇ schema lives in suite 5 | **real mutation**: deleting the `includeStatistics` declaration from a copy of io_operators.cpp is caught by the scanner; scanner unit tests plant drift on synthetic sources |
| 3 | `test_command_contract_9` | L2 | help ↔ command ids (#869); CTA/lookup refs resolve (#882); preflight action resolution (#881); HelpContentStore single-load + duplicate rejection (#871) | HelpContentStore temp-dir regression (double-load collides); allow-list rot guards fail stale entries |
| 4 | `test_diagnostics_contract_9` | L2 | enum ↔ toString switch equality; harness/operator code census vs curated pages (#870); preflight raw-code enumeration | census helper red-direction: a catalog with one page removed reports exactly that code |
| 5 | `test_capability_contract_9` | L2 | operator floor via tool catalog; tool-schema ⊇ operator-schema projection; capability entries resolve (registry ∪ tool ids); recipe refs resolve; duplicate-tool hygiene | live-tree violations fail with operator/entry evidence; floors use ≥-bounds with idiom sentinels |
| 6 | `benchmark_contract9` | L7 | contract generation cost evidence (exp.bench.contract9.v1) | — |
| 7 | existing `test_help_coverage`, `test_capability_drift`, `test_algorithm_meta_drift`, `test_algorithm_schema`, `test_cli_commands_json`, `test_io_operators` | L2 | regression anchors: must stay green | pre-existing; test_capability_drift has a master-side failure unrelated to this branch (4 operators without knowledge entries) |
| 8 | L0 header probes | L0 | all 7 `src/contracts` headers self-contained | probe target build |
| 9 | Ladder lanes L0-L8 (+ LS sanitizer lane, honest not-built without an ASan tree) | L5/L8 | end-to-end status honesty | ladder JSON |

## Rule checks encoded

- R1 新 operator 只改实现不改 schema → suite 2 red.
- R2 新 error code 不加 diagnostics → suite 4 red.
- R3 新 command id 不加 help → suite 3 red.
- R4 preflight/CTA 引用不存在的 action id → suite 3 red.
- R5 capability knowledge 落后于 registry → suite 5 red.
- R6 snapshot 与 live graph 不一致 → suite 1 red with regenerate hint.
- R7 扫描器遇到无法解析的结构 → `unresolved` → red（不静默通过）.
- R8 allow-list 条件消失（rot）→ 对应 suite red，强制清理条目.
