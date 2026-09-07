# DOCS_LEDGER

Every doc touched by Reliability 4.0, with the claim and its proving evidence.

| Document | Reason | Claim added/removed | Code/tests proving it |
|---|---|---|---|
| docs/adr/0130-data-plane-runtime-reliability-4.md | Durable architecture/compat/data-ownership decisions (new ADR) | + document-authority model, downgrade guard, WAL snapshot policy, checked-write contract, reference-safe CAS, cache external-mutation identity, resume identity, truthful run states, worker pool | test_workspace_project_v3 (fault trio), test_workspace_services (WAL snapshot trio, watcher), test_governance_store (lineage/alias/counts), test_fault_injection (CAS eviction), test_workflow_cache_e2e (#749), test_workflow_run_coordinator (#750), test_worker_host (pool) |
| docs/architecture/FAULT_MATRIX_4.md | Epic-required fault-injection matrix | + 24 fault rows with expected safe behavior + coverage pointers | Per-row test pointers in the table |
| CHANGELOG.md | User/developer-visible changes | + Reliability 4.0 section; removed no claims | Same suites as ADR 0130 row |
| CONTEXT.md | Domain vocabulary + ADR index | + Worker Pool term; + ADR 0130 index entry | local_worker_pool tests; ADR 0130 evidence |
| PROJECT.md | Stale PR-integration sprint snapshot replaced with a living project-state doc (also #760's substance) | − stale #708–#712 integration claims, stale worktree/test counts; + durable capability map, build/test policy, doc map | Architecture pointers; suites listed in this ledger |
| .planning/data-runtime-governance-4/SCALE_BASELINE.md | Epic-required benchmark baseline | + 100k timings after hardening (ingest 1125 ms, paged 76 ms, facet 5–22 ms, lookups 39 ms, bulk tag 15 ms, lineage <1 ms) | `SICNU_WS3_STRESS=1 ctest`-run of tests/test_workspace_stress, 2026-09-06 |
| .planning/data-runtime-governance-4/* | Goal-mandated dossier (GOAL/BASELINE/ARCHITECTURE/PLAN/TEST_MATRIX/REVIEW_LOG) | + milestone plan, audited architecture, review rounds | — |

Claim audit note: no doc claims "atomic", "lossless", "bit-identical", or
"fully supported" beyond what the listed tests pin. The downgrade-refusal
branch is documented as loud-but-best-effort (QGIS's writeProject signal
cannot veto the file write) — the practical data-loss routes are closed by
cache-on-read, which the fault tests pin.
