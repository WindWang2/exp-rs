# ADR 0142: Capability Knowledge Layer & Intent Graph (Harness 7.0)

Status: Accepted · Date: 2026-09-09 · Owner: agent/harness

## Context

Harness 4.0 gave the Pi agent a plan compiler, deterministic preflight, and a
recipe catalog, but the *scientific facts* about capabilities lived in three
divergent places: implicit C++ preflight rule packs (keyed by intent), prose
`metadata()`/limitations strings on operators, and informational recipe
fields. Only 12 of ~140 registered operators declared any `x-rs-contract`;
recipes duplicated one file per sensor variant (102 files, 21 duplicate
groups); run-path verification used near-vacuous defaults; repair was
advisory-only; and the agent's context had no slots for experiments, plan
bindings, verification status, or open decisions.

## Decision

1. **Machine-decidable capability knowledge** (`data/agent/capabilities/*.json`,
   loader `src/agent/harness/capability_knowledge.{h,cpp}`): typed entries
   keyed by operator id with applicability, input modality, band-role
   minimums, radiometric preference, CRS demands, SAR requirements, temporal
   requirements, model compatibility, resource hints, side effects, artifact
   contracts, verification checks, and limitations. Family defaults +
   explicit `extends` + parameter-conditional `variants` keep the catalog
   compact. Load-time validation is closed-vocabulary and fail-closed.
2. **Anti-drift as a test, not a hope** (`tests/test_capability_drift.cpp`):
   entry ids must resolve in the live operator registry; modality must agree
   with operator-declared `x-rs-contract` facts; every intent in the closed
   plan vocabulary must be served; and the preflight rule packs expose
   `intentRequirements()` — a machine-readable mirror of the dispatch table —
   whose band/min-scene demands the knowledge layer must satisfy (exactly for
   index intents, by coverage for science intents).
3. **Intent → capability graph** (`capability_graph.{h,cpp}`, tool
   `harness:resolve_intent`): deterministic keyword-evidence classification
   into the closed intent vocabulary; ties and no-evidence cases return a
   typed `INTENT_AMBIGUOUS` with candidates and matched evidence — never a
   guess. Feasibility of every serving capability is evaluated against
   DatasetUnderstanding facts (modality, band roles, radiometry, temporal) and
   ranked with why/why-not reasons.
4. **Preflight 7.0**: the if/else dispatch became a specification table
   (`IntentSpec`); new packs for segmentation/model inference (model-manifest
   vs dataset contract), multimodal optical+SAR fusion, flood physics
   caveats, temporal-series facts, land-cover legends, and accuracy
   cross-domains. Facts narrow checks; unknown facts warn, never block or
   fake.
5. **Bounded plan repair** (`harness:repair_plan`): ≤3 passes of structurally
   safe surgery only (duplicate step-id rename, dangling-output drop);
   science choices stay advisory. Transient resume attempts moved to a global
   per-run ledger (≤3), closing the poll-loop retry hole.
6. **Derived verification expectations**: run verification now layers (a)
   structural defaults, (b) capability-knowledge verification checks for the
   plan intent (finite/nodata thresholds, provenance, uncertainty), and (c)
   the plan's own `verification.expectations` block (most specific wins).
   New checks: `extent_covers_aoi`, `uncertainty_present`.
7. **Typed context continuity** (`context_ledger.{h,cpp}`): bounded,
   process-scoped stores for plan/run bindings with verification status,
   typed decisions (record/resolve/list via `harness:decision_record`), and a
   dataset-understanding cache keyed by (path, asset revision). Context rides
   the existing `harness:context` revision contract. This is authoritative
   state, not chat memory. An `experiments` provider seam mirrors the
   workflow-runs seam for the dataset/experiment platform.
8. **Recipe de-duplication**: `presets` (flat param overrides + `step_params`
   + `keep_outputs`) and `aliases` let 29 near-clone files collapse into
   canonical documents (102 → 73) without breaking references. Every preset
   is validated at load and its application is deterministic.
9. **Enforced token budgets**: every tool registration is wrapped in a
   metering decorator; outputs over `kMaxToolOutputBytes` (512 KiB) are
   compacted schema-aware (largest array member trimmed first) with explicit
   truncation markers — the cap now has teeth at the single choke point.

## Consequences

- Pi plans against *decidable* knowledge and ranked, feasibility-explained
  candidates; ambiguity is a typed, recoverable surface.
- Science facts have one derivation target per layer, pinned by drift tests.
- The recipe catalog stops growing per sensor/filter/classifier variant.
- FAIL-never-success semantics are unchanged; derived expectations only
  tighten checks when a plan or knowledge contract declares them.

## Non-goals

- No second agent loop, no scheduler changes, no chat memory. Plans still
  compile to the single WorkflowDefinition seam and run through
  WorkflowRunCoordinator → TaskCenter.
- Dataset/experiment agent tools are owned by the parallel
  `feat/dataset-experiment-7` track; this layer defines only the provider
  seam and typed slots.
