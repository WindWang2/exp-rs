# DOCS_LEDGER — Harness 4.0

Every documentation change, why it was made, and what proves it.

| Document | Change | Why | Proven (code / test) |
|---|---|---|---|
| `docs/agent/spatial-scientist-architecture.md` | NEW — harness layering, invariants, namespace table, plan lifecycle, known limits | mission-required doc; records the division "Pi generic / ExpRS spatial" | layering matches `src/agent/harness/*`; limits match code (provenance warning, P1-E1) |
| `docs/agent/tool-contracts.md` | NEW — manifest wire shape, taxonomy vocabulary, risk classes, error contract | mission-required doc; the closed vocabularies are load-bearing for Pi | `tool_manifest.{h,cpp}`, `tool_taxonomy.{h,cpp}`, `harness_error.{h,cpp}`; `test_harness_catalog`, `test_harness_error` |
| `docs/agent/scientific-preflight.md` | NEW — rule packs, verdict semantics, wavelength fallback, veto rule | mission-required doc | `scientific_preflight.{h,cpp}`; evals SAR/classify blocking |
| `docs/agent/result-verification.md` | NEW — tri-state verdicts, check list, run result doc, map confirmation, bounded retry | mission-required doc | `harness_verification.{h,cpp}`, `plan_tools.cpp`; eval FAIL-never-success |
| `docs/agent/evaluation-suite.md` | NEW — eval dimensions, 6 scenarios, determinism contract | mission-required doc | `tests/test_harness_evals.cpp` |
| `docs/agent/workflow-integration.md` | NEW — compile chain, execution state model, plan schema v2 | mission-required doc (execution state model) | `agent_plan.{h,cpp}`, `workflow_run.h`, `workflow_run_coordinator.h` |
| `docs/agent/pi-adapter.md` | NEW — adapter bootstrap, responsibility split, subagents, failure semantics | mission-required doc (Pi adapter architecture) | `pi/exp-rs-spatial.ts`, `pi/roles/README.md` |
| `pi/roles/README.md` | NEW — 5 subagent role cards + single-writer rule | mission Phase 17 | reviewers read-only by tool list; risk classes in `tool_manifest.cpp` |
| `pi/exp-rs-spatial.ts` | default `EXP_RS_TOOL_CATEGORIES` += `harness,layout` | harness tools must reach Pi | adapter filter `wantedCategories()` |
| `.gitignore` | re-include `data/agent/**` | recipes are tracked metadata like `data/cartography` | `git check-ignore` clean |
| `data/agent/recipes/*.json` | NEW — 5 recipes | mission Phase 16 | `recipe_catalog.cpp` loader; evals instantiate all |
| `CONTEXT.md` | ADR index + new domain terms (Harness 4.0) | architecture sync (final integration, additive) | this ledger |
| `CHANGELOG.md` | Harness 4.0 entry | user/developer-visible changes | code+tests above |
| `PROJECT.md` | refresh to current capabilities | mission docs constraint (remove stale 3.0-series sprint info) | final pass |
| `docs/adr/0130-harness-4.md` | NEW ADR | long-term architecture decision record for the harness | final pass |

## Claim-to-code audit (strong words)

- "deterministic preflight/verification/recipes/evals" — backed: rule packs
  are pure functions over inspected facts; evals assert exact codes/verdicts.
- "no false success after FAIL" — backed: `runResultDocument` forces
  `status: "failed"`; eval asserts FAIL aggregation; no inverse path exists.
- "bounded responses" — backed by eval token-budget assertions; registry cap
  (512 KiB) documented, not yet enforced at the registry seam (P2, see
  REVIEW_LOG).
- "cross-platform" — NOT claimed in docs; Windows build fixes are recorded as
  repairs of pre-existing master defects (qgsproject fsync, S_ISREG).
