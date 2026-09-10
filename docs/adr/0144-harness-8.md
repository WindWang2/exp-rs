# ADR 0144: Spatial Scientist Harness 8.0 — Evidence-Driven Deepening

Status: Accepted · Date: 2026-09-11 · Owner: agent/harness

## Context

Harness 7.0 (ADR 0142) delivered machine-decidable capability knowledge, the
intent graph, spec-table preflight, bounded plan repair, typed context
continuity, and enforced token budgets. The 8.0 mission keeps Pi as the only
agent loop and deepens the evidence chain instead. The verified gaps:

1. Knowledge covered 91 of ~132 registry operators and no platform tools
   (`dataset:*`, `experiment:*`, `workflow:*`, `cartography:*`, `model:*`
   surfaces); the drift guard only resolved operator ids.
2. DatasetUnderstanding folded product facts into one field — the last
   SICNU_* key won, so spacecraft/acquisition-date clobbered the radiometric
   state. No sensor/product/time slots, no NoData or quality-mask slots, no
   per-asset context revisions or model-contract slots.
3. No evidence sidecar writers existed (7.0 adversarial review F1): no
   uncertainty sidecars, no verification-evidence persistence, no quality
   summary; provenance existed only on the engine's temp-output path.
4. Plans had no identity pins, no cleanup policy, no deterministic
   fingerprint; verification had no band-count or identity checks; uncertainty
   presence was permanently warning-class even when an operator declares
   uncertainty facts.
5. Intent resolution reported blockers but no missing facts, no preparation
   actions, no recipe-level solution paths; no explainability surface.
6. Evaluations were C++-embedded only; no externalized, versioned corpus.

## Decision

1. **Typed spatial context 2.0**: DatasetUnderstanding gains typed slots
   (`sensor`, `product_type`, `product_id`, `processing_level`,
   `acquisition_time`, `radiometric_state`) plus a dataset-level SICNU_*
   passthrough, sparse `nodata`, and `quality_masks` — each fact exactly one
   slot, validator-checked. `ContextLedger` records per-asset contexts with
   read-time stale detection (stat identity) and model contracts (bounded).
2. **Capability knowledge completion**: entries gain a `surface`
   discriminator (`operator` default, `spatial_tool`, `data_platform_tool`);
   tool-surface entries never declare scientific intents. New knowledge
   covers every registered operator (full-registry coverage floor is a drift
   test) and the platform tool families. Recipe `capabilities` chains are
   drift-pinned to registry + knowledge.
3. **Evidence sidecars** (`evidence.{h,cpp}`): harness-owned, atomic
   (QSaveFile) writers beside the artifact:
   `<out>.provenance.json` (run identity, never overwriting the engine's
   derivation sidecar), `<out>.uncertainty.json` (only from operator-declared
   result facts — no fabrication), `<out>.verification.json` (verdict,
   checks, expectations, quality summary, run identity). Declared-but-
   unwritable uncertainty is an error-class check (FAIL); absence where no
   method declares uncertainty stays advisory.
4. **Plans 8.0**: `pins` (dataset identity, validated against resolved
   entities at execute; IDENTITY_MISMATCH joins the stable taxonomy), model
   pin, `cleanup` policy and per-step `role` (closed vocabularies),
   deterministic `plan_fingerprint` (SHA-256 over canonical science content)
   — carried in bindings, run documents, and every sidecar; policies forward
   into compiled workflow `metadata` (engine parser ignores unknown keys;
   consumption is an execution-plane follow-up).
5. **Verification**: `expected_band_count` check; evidence-write outcomes
   appended post-verification with verdict recomputation (FAIL-never-success
   preserved).
6. **Intent/feasibility 2.0**: `harness:resolve_intent` also reports
   `missing_facts` (capability demands vs understanding slots),
   `preparations` (static why_not-code→action table; codes without a safe
   preparation are marked, never guessed), and `solution_paths` (recipes
   serving the intent).
7. **Explainability**: `harness:explain {run_id, plan?}` assembles data
   used (identity/revision), method applicability, what executed, verification
   evidence, assumptions, unknowns (stale contexts, unresolved decisions)
   from authoritative stores only.
8. **Preflight 8.0**: preflight documents expose `assumptions` — the
   warning-class issues — separating honest unknowns from blockers.
9. **Eval corpus**: `data/agent/evals/cases/*.json` with a closed category
   vocabulary, `foreach` expansion, runtime-generated fixtures, and a
   deterministic Tier-A runner (`tests/test_harness_eval_corpus.cpp`) that is
   also a schema guard (unique ids, live tools, bounded expansion).

## Consequences

- Every run now leaves inspectable evidence next to its artifacts; an agent
  answer can cite files, not memory.
- Knowledge covers the live platform mechanically; the drift tests fail for
  any new operator or platform tool without knowledge.
- Context survives turns with explicit staleness instead of silent reuse.
- The engine parser's tolerance of workflow `metadata` is load-bearing for
  cleanup/pins forwarding; execution-plane-8 owns any stricter behavior.

## Non-goals

- No second agent loop, scheduler, memory store, or model runtime.
- No uncertainty fabrication: operators own algorithm-specific facts.
- Transient-failure engine scenarios and dataset/experiment store
  interactions stay in their existing suites (engine tests; MCP-surface
  tools) — the corpus is the tool-contract plane.
