# ADR 0130: Pi Spatial Scientist & Agent Harness 4.0

## Status

Accepted (implemented on `zcode/pi-spatial-scientist-harness-4`).

## Context

Spatial Scientist 3.0 (ADR 0127/0128) gave agents structured contract
documents, bounded inspection tools, and a 111-task benchmark — but the
Phase-0 audit for 4.0 (12 workflow traces, 20 gaps) showed the harness still
forced the model to guess: grounding tools took raw paths instead of stable
ids, tool metadata (risk, resources, preconditions, artifacts) was never
serialized, preflight checked DAG structure but not science, plans were
defined-but-unwired documents, MCP execution results carried no verification,
map correctness after workflows was a by-convention loop, and no stable error
taxonomy existed across surfaces.

## Decision

Add a harness layer under `src/agent/harness/` (namespace `sicnu::agent::harness`),
exposed as `harness:*` SpatialTools through the single catalog:

1. **Error taxonomy** — closed stable codes (`DATASET_NOT_FOUND`, …,
   `MAP_PREFLIGHT_FAILED`) with category + retry class and machine-actionable
   suggestions; legacy codes normalize into it.
2. **Tool manifests + taxonomy** — every catalog entry gains a bounded
   `harness` block (taxonomy `domain.action`, risk class, side effects,
   resource hints, cancellation, preconditions, expected artifacts) derived
   from `AgentMetadata` / a namespace risk table; `outputSchema` finally
   reaches the wire.
3. **Grounding** — `EntityResolver` resolves entity id / UUID / path / display
   name against authoritative registries; ambiguity is a typed failure with
   candidates; `spatial:understand` returns a DatasetUnderstanding document
   with deterministic modality inference.
4. **Typed context** — `harness:context` with content-hashed revisions;
   unchanged revisions short-circuit to `{unchanged:true}`.
5. **Scientific preflight** — deterministic intent rule packs (ndvi, change,
   sar_change, classify, phenology) over inspected facts; `blocked` vetoes
   execution.
6. **Plan lifecycle** — AgentPlan v2 (goal/inputs/steps/outputs/verification/
   map_output) with one compiler to `WorkflowDefinition`; execution stays in
   `WorkflowRunCoordinator` → `TaskCenter` (no second engine); per-step +
   aggregate resource estimates before submission.
7. **Verification** — `PASS | PASS_WITH_WARNINGS | FAIL` per artifact and for
   the run; FAIL forces status `failed` (no false success). Map-producing
   plans get a final map confirmation (layout presence, MapSpec
   preflight/repair loop, export gating).
8. **Bounded retry** — transient failures may auto-resume once via the
   engine's own resumeRun; destructive/non-idempotent work never auto-retries.
9. **Recipes** — five metadata-driven recipes (`data/agent/recipes/*.json`)
   that instantiate plans from bound slots; operators remain the only
   algorithms.
10. **Evals** — six deterministic scenario evals (two executed end-to-end on
    real operators) plus anti-hallucination and token-budget assertions.

## Consequences

- Pi reads codes, manifests, and typed documents; guessing paths for
  tool parameters is no longer necessary anywhere on the harness surface.
- Tool count growth stays bounded: new tools must classify into the closed
  taxonomy and risk table or the catalog tests fail.
- The mission's strong claims are enforced in code where feasible
  (FAIL ⇒ failed) and documented as limits where not (provenance sidecars on
  the MCP workflow path; registry-wide output-size cap).
- Known follow-ups: wire `setOutputVerificationHandler` for MCP single-call
  results; register MCP workflow-run outputs via OutputCommitter (P1-E1);
  enforce the 512 KiB cap at the registry seam.
