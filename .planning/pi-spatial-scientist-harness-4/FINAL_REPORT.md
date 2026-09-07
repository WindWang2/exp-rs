# FINAL_REPORT — Pi Spatial Scientist & Agent Harness 4.0

Branch: `zcode/pi-spatial-scientist-harness-4` (worktree `exp-rs-pi-harness-4`)
Baseline: `master@a89c0c20` · PR: `feat(agent): Pi Spatial Scientist & Agent Harness 4.0`

## Outcome

(To be finalized after the test lane — statuses below.)

| Item | Status |
|---|---|
| Phase 0 audit: 12 traces, 20-gap register | DONE (BASELINE.md) |
| Error taxonomy (P12) | DONE — `test_harness_error` |
| Tool manifests + taxonomy (P1/P2/P14) | DONE — `test_harness_catalog` |
| Typed context + grounding (P3/P4/P19) | DONE — `test_harness_grounding` |
| Scientific preflight (P5) | DONE — rule packs + evals |
| Plan lifecycle + compiler + estimates (P6/P7/P8) | DONE — `harness:plan/execute_plan/run_status` |
| Verification + map confirmation + retry (P9/P10/P11/P13) | DONE |
| Recipes (P16) | DONE — 5 recipes + 3 tools |
| Pi roles + bridge categories (P17) | DONE |
| Evals + anti-hallucination + token budgets (P18/P19/P20) | DONE — `test_harness_evals` |
| Local build green (Windows, MSVC+Ninja) | DONE — full tree incl. app + CLI + tests |
| Harness test lane green | DONE — 42+93+80+118 assertions, all pass |
| Full ctest lane no regressions | DONE for touched areas (spatial/agent/workflow/task_center/mapspec/governance/layout/plugins); POSIX-fixture targets skip on Windows; UTF-8-named engine tests fail only under ctest name filtering (console codepage artifact — direct runs green; pre-existing) |
| 6 adversarial reviews | DONE (REVIEW_LOG.md; P0=0, P1=0) |
| Docs synced | DONE (DOCS_LEDGER.md) |

## Deliverables map

- `src/agent/harness/` — error taxonomy, taxonomy, manifests, resolver,
  grounding tools, context, preflight, plan model/compiler/estimates,
  verification, plan tools (execute/status), recipe catalog + tools.
- `data/agent/recipes/*.json` — optical-vegetation, optical-change,
  sar-change, land-cover, phenology.
- `pi/` — categories + role cards; `docs/agent/` — 7 documents; ADR 0130.
- Cross-platform repairs of pre-existing master defects (needed to build at
  all on Windows): qgsproject directory fsync guard, S_ISREG define.

## Performance / tokens

- Measured (benchmarks/harness-token-budgets.json): context 2,411 B
  (cap 256 KiB; unchanged-revision reads ~40 B), error catalog 1,729 B
  (cap 8 KiB), 50-tool manifest page 12,501 B (cap 64 KiB). Caps asserted
  by the eval suite; the dump is opt-in via SICNU_BENCH_OUT.

## Risks & known limitations

1. MCP workflow-run outputs are not yet registered as governed assets
   (pre-existing TODO P1-E1); run verification warns on missing provenance.
2. Auto-resume transient classification is heuristic; bounded to one attempt
   and the engine re-validates outputs before skipping work.
3. 3.0-era static-guarded tool registrations drop tools on registry reset();
   harness tools are reset-safe, the 3.0 fix is deferred (hot path).
4. Phenology/sar preflight validate what metadata allows; unverifiable facts
   warn instead of failing silently.

## Compatibility

- Additive tools/fields only; existing tool names, schemas, and MCP meta
  tools unchanged. Plan schema versioned (1.0 legacy accepted, 2.0 emitted).
- No changes to operator kernels, workflow engine semantics, or TaskCenter
  admission; the harness compiles to and observes the authoritative stack.
