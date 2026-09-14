# BASELINE — scientific-agent-workflow-compiler-10

- **Baseline SHA**: `origin/master @ 7d78059d1a6d316d606656759a506d17bc5e3b55`
  ("Merge pull request #958 from WindWang2/zcode/prompt-command-hygiene-review")
- **Worktree**: `../exp-rs-scientific-agent-workflow-compiler-10`
- **Branch**: `zcode/scientific-agent-workflow-compiler-10`
- **Verified**: `git fetch --all --prune` → `git checkout master && git pull --ff-only`
  → "已经是最新的" → rev-parse matches SHA above.

## Open concurrent 10.0 tracks (dedupe exclusions)

| PR | Branch | Scope | Overlap risk |
|---|---|---|---|
| #975 OPEN | zcode/scientific-contract-verification-10 | scientific correctness/contracts of algorithms | consumes harness contracts; no harness-file ownership claimed yet |
| #974 OPEN | zcode/cloud-data-fabric-datacube-10 | data fabric / virtual cubes / query planner | `src/geospatial/`; none in harness |
| #973 OPEN | zcode/temporal-eo-phenology-change-10 | temporal algorithms; claims `docs/adr/0148-temporal-platform-10.md` | temporal operators; none in harness |
| #972 OPEN | zcode/r2-deep-review | review dossier only | none |

## Direct predecessor tracks (merged)

- **spatial-scientist-harness-8** (ADR 0144-harness-8): typed context 2.0, evidence
  sidecars, capability knowledge, eval corpus, pins/cleanup.
- **spatial-scientist-harness-9** (ADR 0145): closed action vocabulary (#881), recipe gate
  truth (#867), condition NaN/has (#866/#877), typed intents (zonal), advice tier +
  preparation table, `harness:diagnose_run`, bounded run summaries w/ token meter,
  explain 2.0, eval corpus 91 cases, capability-knowledge coverage floor fix.
- **agent-capability-knowledge (D8)** (ADR 0154 sidecars + capability-relation-graph):
  111/111 v2 capability sidecars, 11 families, relation graph (chains/exclusive/
  requires_shared_grid), `composeChain` w/ automatic grid fixer (`rs:align`),
  `harness:compose_chain`, bounded manifest (64 KiB) + error catalog (8 KiB).

## Dedupe exclusion table (must NOT re-implement)

| Already on master | Where | Verdict |
|---|---|---|
| AgentPlan v2 reader/validator/compiler (schema 2.0, v1 compat) | `src/agent/harness/agent_plan.*` | consume; IR compiles THROUGH it |
| Closed intent vocabulary + typed intent docs | `intent_vocabulary.h`, `scientific_preflight.cpp` | consume; extend, never re-spell |
| Feasibility/candidates/missing-facts/preparation | `capability_graph.cpp` | consume for planner stage |
| Relation graph + composeChain w/ grid fixer | `capability_relations.*` | reuse as one repair source |
| Error taxonomy (closed codes + retry classes) | `harness_error.*` | extend only via declared constants |
| Evidence sidecars + verification evidence read-back | `evidence.*`, `harness_verification.*` | consume for IR evidence projection |
| ContextLedger (bindings/decisions/understanding cache/asset contexts/model contracts/run summaries + token budget) | `context_ledger.*` | extend w/ checkpoint; no second ledger |
| diagnose_run bounded proposals | `run_loop.*` | extend to typed loop; no second loop |
| manifestPage 64 KiB / errorCatalog 8 KiB budgets | `capability_catalog.*` | reuse budget pattern for shortlist |
| Engine checkpoint/resume/cache/impl stamps | `src/workflow/`, ADR 0123 | consume; harness checkpoint is the CONTEXT layer only |
| Closed action vocabulary + teaching gate | `harness_actions.*` | consume for all new suggestions |
| Eval corpus (17 categories, 91 cases) | `data/agent/evals/`, `tests/test_harness_eval_corpus.cpp` | extend with new closed categories |

## Relevant open findings (review/findings/)

- **F-PI-1** (P2, in-scope): bridge line-buffer overflow leaves zombie stream — must
  kill/respawn on overflow. `pi/mcp_bridge.ts:176-198`, `pi/exp-rs-spatial.ts:202-224`.
- **F-PI-2** (P2, in-scope): startup-deadline success-path cleanup missing in
  `exp-rs-spatial.ts` (fix drifted one-way from `mcp_bridge.ts:158-172`).
- F-OPS-1..5: `src/operators/` — OUT OF SCOPE (algorithms track); F-OPS-3 (qa_mask
  fail-open) and F-OPS-4 (io:reproject dead param) recorded as constraints on repair
  design: repair table must not rely on `rs:qa_mask` NoData semantics or
  `io:reproject srcCrsOverride` until fixed upstream.
- #869–#871 help-store drift: unified-help-diagnostics-6 lane, not ours.

## Harness inventory at baseline (sizes = lines)

plan_tools 1588 · scientific_preflight 1319 · capability_catalog 1070 · recipe_catalog
1009 · capability_graph 785 · capability_knowledge 748 · capability_relations 625 ·
grounding_tools 535 · lab_copilot 482 · agent_plan 470 · run_loop 435 · solution_tools
418 · harness_verification 416 · context_ledger 334 · lab_spec 302 · entity_resolver 294
· capability_pages 271 · evidence 257 · tool_taxonomy 222 · recipe_tools 213 ·
harness_tools 211 · harness_error 211 · tool_manifest 197 · lab_tools 179 ·
harness_actions 178 · band_facts 172 · (headers included).

Tests: test_harness9_contracts (289 assertions), test_capability_drift,
test_harness_eval_corpus (91 cases), test_harness_evals, test_harness_grounding,
test_harness_evidence, test_harness_error, test_spatial_contracts, test_agent_tools_3,
test_spatial_scientist_benchmark — all green on baseline per harness-9 FINAL_REPORT
(re-verified locally in Phase 8).
