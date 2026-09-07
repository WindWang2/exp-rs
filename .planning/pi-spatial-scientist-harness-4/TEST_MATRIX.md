# TEST_MATRIX — Harness 4.0

Local lane: `ctest --test-dir build -C Release -R harness --output-on-failure`
(Windows: MSVC 14.38 + Ninja, `CTEST_PARALLEL_LEVEL=1`). Full-suite lane
before PR: `ctest --test-dir build -C Release --output-on-failure`.

| Target | Covers | Mission phases |
|---|---|---|
| `test_harness_error` | GREEN (42 assertions). Closed code vocabulary, category/retry-class mapping, envelope fields, legacy normalization | 12 |
| `test_harness_catalog` | GREEN (93). Taxonomy totality + closed vocab, risk classes incl. every mutating namespace, manifest fields, bounded manifest size, harness tool registration + typed failures | 1, 2, 14 |
| `test_harness_grounding` | GREEN (80). Modality inference rules, entity resolution (path/entity/unknown/empty), typed failures, spatial:understanding document fields, context revision short-circuit + refresh | 3, 4, 19 |
| `test_harness_evals` | GREEN (118). 6 scenarios (NDVI + optical change executed end-to-end on real operators; SAR/classify/phenology/figure contracts), anti-hallucination codes, FAIL-never-success, token budgets; opt-in benchmark dump via SICNU_BENCH_OUT | 18, 19, 20 (+5,6,7,9,10) |
| pre-existing | `test_spatial_tools`, `test_spatial_contracts`, `test_agent_tools_3`, `test_spatial_scientist_benchmark`, `test_workflow_*`, `test_task_center`, `test_mapspec`, … must stay green (no regressions) | — |

## Execution-state coverage notes

- Engine state machine, checkpoints, resume, cancel, cache: covered by the
  pre-existing `test_workflow_*` suites (not duplicated).
- `harness:run_status` verification + status forcing: covered by eval
  scenarios 1–2 (PASS path) and the FAIL-never-success case.
- Bounded transient resume: engine-side `resumeRun` covered by
  `test_workflow_run_coordinator`; harness-side classification is reviewed in
  REVIEW_LOG (P2 note: transient sniffing is heuristic).

## Windows lane notes (this epic's local verification)

- Direct-exe runs of the four harness suites + `test_agent_golden_workflow`,
  `test_fused_chain`, `test_layout_tools`, `test_help_system`,
  `test_plugins_runtime_host`, `test_exprs_plugin_loader` — all green.
- ctest name-filter failures for tests whose NAMES contain UTF-8 arrows/
  dashes (`→`, `—`) are a Windows console-codepage discovery artifact; the
  binaries pass when run directly (pre-existing, not from this epic).
- POSIX-fixture targets gated `if(NOT WIN32)`: test_execution_benchmarks,
  test_remote_source_cache, test_fault_injection, test_workflow_cache_e2e,
  test_cli_commands_json, test_exprs_plugin_system.
