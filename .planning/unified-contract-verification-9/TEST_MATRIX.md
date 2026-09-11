# TEST_MATRIX — Contract Platform 9.0

Verification standard: **old code must fail, new code must pass** for every
guard; mutation tests prove the red direction mechanically. Statuses:
`pass / fail / skip / not-built / not-run / unsupported` — never aggregated
into green.

| # | Suite | Level | Guards (issue class) | Red evidence |
|---|-------|-------|----------------------|--------------|
| 1 | `test_contract_platform_9` | L2 | graph integrity: duplicate ids, dangling refs, phantom nodes on live tree; snapshot byte-compare | unit-tested on synthetic graphs; snapshot regeneration documented |
| 2 | `test_contract_projection_9` | L2 | impl-reads ⊆ schema (#872/#879/#880); dead schema params; descriptor round-trip; listSchemas ↔ schema(); MCP tool schema ⊇ schema; algorithm_meta ids resolve | scanner unit tests with planted drift; mutation: delete schema field from copied real source → scanner red |
| 3 | `test_command_contract_9` | L2 | help ↔ command ids (#869); preflight/CTA/lookup refs resolve (#881/#882); phantom help; duplicate help ids; HelpContentStore single-load (#871) | mutation on temp copies of data/help JSON + synthetic sources |
| 4 | `test_diagnostics_contract_9` | L2 | every error enum value → curated diagnostics page or explicit allow-list (#870); retry-sense agreement | synthetic enum/header pair in temp dir through the same scanner |
| 5 | `test_capability_contract_9` | L2 | operator/tool/knowledge floors; model modality agreement; recipe/mapspec/plugin refs resolve | synthetic knowledge/tool fixtures in temp dirs |
| 6 | `test_contract_mutation_9` | L2 | the guards themselves: planted drift in synthetic + copied-real sources must be caught | is the red evidence |
| 7 | `benchmark_contract9` | L7 | contract generation cost at current scale (operators/files), JSON out | — |
| 8 | existing `test_help_coverage`, `test_capability_drift`, `test_algorithm_meta_drift`, `test_shortcut_conflicts`, `test_cli_commands_json` | L2 | regression anchor: must stay green | pre-existing |
| 9 | L0 header probes | L0 | new `src/contracts` headers self-contained | probe target build |
| 10 | Ladder lanes L0-L8 | L5/L8 | honest statuses end-to-end | ladder JSON |

## Rule checks encoded

- R1 新 operator 只改实现不改 schema → suite 2 red.
- R2 新 error code 不加 diagnostics → suite 4 red.
- R3 新 command id 不加 help → suite 3 red.
- R4 preflight/CTA 引用不存在的 action id → suite 3 red.
- R5 capability knowledge 落后于 registry → suite 5 red.
- R6 snapshot 与 live graph 不一致 → suite 1 red with regenerate hint.
- R7 扫描器遇到无法解析的结构 → `scan_unresolved` → red（不静默通过）.
