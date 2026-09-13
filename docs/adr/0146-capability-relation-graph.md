# ADR 0146: Capability Relation Graph & Operator Capability Catalog (D8)

Status: Accepted · Date: 2026-09-13 · Owner: agent/harness
Charter: D8 — RS Agent Capability Knowledge Layer (111 算子能力知识层)

## Context

ADR 0142 gave the agent machine-decidable *feasibility* knowledge
(`data/agent/capabilities/`, 111/111 entries) and a deterministic intent graph.
Two capability questions still force the agent to guess, because no data
answers them:

1. **Composition** — "which operator may legally follow which, and what must
   run first?" The only sources were recipe documents (a curated minority) and
   LLM priors. Nothing says `rs:change_difference` needs both epochs on one
   grid, or that `rs:pca` is a legal feature-reduction step before supervised
   classification.
2. **Reproducibility** — ADR 0124 grades (`bit_exact` / `tolerance`) exist in
   operator schemas, but nothing surfaces them as a queryable, per-operator
   fact with a "may not reproduce" advisory.

Additionally the agent's *teaching/serving* knowledge — per-operator failure
modes, land-cover applicability, classroom use — existed only as prose in one
hand-written page (`pi/knowledge/spatial-algorithm-guide.md`), and the
`data/processing/algorithm_meta/` sidecars (ADR 0122) covered 30/111 operators
with a sparse task/input/output schema.

## Decision

1. **v2 capability sidecars** under
   `data/processing/algorithm_meta/capability/rs-<slug>.json` — one file per
   `rs:` operator (111/111, binary gate). Each sidecar = the ADR 0122 sparse
   overlay fields **plus** a typed `capability` block: `family` (11 canonical
   families), `io` (inputs/outputs/parameters split from the descriptor
   ports), `modality`, `band_roles`, `crs.requires_shared_grid`, `determinism`
   (ADR 0124 grade + stochastic/memory/cost facts), `prerequisites`,
   `limitations`, and authored-only `summary` / `failure_modes` /
   `applicability` / `teaching_use`. Loading is closed-vocabulary and
   fail-closed (`capability_catalog.h`).
2. **Derivation discipline**: everything mechanically derivable is generated
   from the live `AlgorithmDescriptor`s by
   `scripts/capability_knowledge_tool gen-meta`; authored keys are preserved on
   regeneration. The drift test re-derives in-process and fails the build on
   any divergence — descriptors stay the single source of truth. A sidecar is
   never hand-written where a code fact exists.
3. **Relation graph as data**:
   `capability/` + `capability_relations.json` with three relations —
   `chains` (A→B legal-follow edges with fact-gated `when` conditions),
   `exclusive` (A⊕B alternative-implementations pairs), and
   `requires_shared_grid` (operators demanding one shared grid, the ADR 0098
   contract; `rs:align` is the canonical fixer). The graph is validated at
   load: no dangling operator ids, chains form a DAG, exclusive pairs never
   double as chain edges.
4. **Deterministic composition**: `composeChain(target, facts)` walks the
   graph — fact-gated upstream closure, exclusivity checks, automatic grid
   fixer insertion — and returns ordered steps, skipped edges with reasons,
   and advisory notes (prerequisites, ADR 0124 reproducibility, stochastic
   warnings). No model call, stable byte-identical output. Exposed as the
   `harness:compose_chain` tool alongside `harness:resolve_intent`.
5. **Stable query API** for D9: `byFamily` / `byInputModality` /
   `determinismOf` / `stochasticOperators` / `requiresGrid` / `chainFrom` /
   `manifestPage` / `errorCatalog`. Browsing is paginated with hard budgets
   (manifest page < 64 KiB, error catalog < 8 KiB, per
   `docs/agent/evaluation-suite.md`).
6. **Generated knowledge pages**: `pi/knowledge/capability-<family>.md` +
   `capability-index.md` (中文) render from the same sidecars; the guard test
   re-renders and byte-compares. Hand-editing a generated page is a defect;
   the `spatial-algorithm-guide.md` remains the hand-written narrative
   companion.

## Consequences

- The agent answers "what runs first, what may follow, what must share a grid,
  what may not reproduce" from a query, not a guess.
- All 111 operators carry enrichment of uniform shape; absence can no longer
  masquerade as inapplicability.
- Descriptor changes that alter capability facts now fail the drift test until
  `gen-meta` regenerates — the same discipline ADR 0122 imposed on the sparse
  overlay, extended to the full v2 schema.
- `pi/knowledge/` pages cannot drift from metadata; content edits happen in
  sidecars (authored keys) only.

## Non-goals

- No changes to operator implementations under `src/operators/` (grades and
  contracts stay declared by the operators themselves).
- No second determinism vocabulary — ADR 0124 grades are referenced verbatim;
  the stochastic marker reuses the descriptor's `deterministic=false` field.
- The ADR 0142 feasibility layer (`data/agent/capabilities/`) is untouched;
  modality/band-role facts are cross-checked between the layers by the guard
  test instead of merged.
