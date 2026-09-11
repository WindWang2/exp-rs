# ADR 0145: Spatial Scientist Harness 9.0 — Typed Actions, Honest Plans, Bounded Repair

Status: accepted · Branch `feat/spatial-scientist-harness-9` · Baseline `origin/master@132da5e998`

## Context

Harness 8.0 delivered typed context, evidence sidecars, capability knowledge
over every operator, and an eval corpus. Post-merge triage (#867, #881 and
the #866/#877 condition semantics) showed the remaining defect class is
semantic drift between what the harness SAYS and what a caller can DO:
unresolvable pseudo-actions, gates that silently degrade numeric
parameters, degradation that produces orphan steps, and condition math that
corrupts under NaN. 9.0 closes that class and deepens the long-horizon loop
without introducing a second agent architecture.

## Decisions

1. **Closed suggested-action vocabulary** (`src/agent/harness/harness_actions.*`).
   Every action the harness attaches to an issue resolves to a registered
   SpatialTool id, a registered workbench command id, or the `author` kind
   (Pi edits the plan it is holding). Unknown keys ship `"resolved": false`
   — drift is visible on the wire. A mechanical floor test cross-checks the
   table against the live tool registry and parses the authoritative
   workbench command source; the app registry stays the only registry.
2. **Recipe gate truth is total** (#867): bool = its value, numeric = bound
   (a bound 0 threshold is a value), string = non-empty, containers =
   non-empty. `$params.X` substitution renders numerics through the JSON
   writer (shortest round-trip), never `asString()`'s binary expansion.
3. **Degradation is transitive and path-precise**: a step whose normal path
   is blocked (own gate closed, or a normal-path dependency dropped/skipped)
   runs its fallback or is DROPPED. Fallback-less steps can no longer survive
   as orphans consuming intermediates nobody produces. Edges arising only
   from `params_when_skipped` never flip a healthy normal template.
   Instantiation emits a bounded `degradations` record (step, mode, reason).
4. **Condition math is IEEE-honest** (#877) and presence is first-class
   (#866): `has(x)` is an operand evaluating to a boolean presence fact; NaN
   is unordered (== false, != true, orderings false) instead of comparing
   equal to everything.
5. **Preflight grades blockers / warnings(assumptions) / advice**, and emits
   a safe **preparation table**: only deterministic data transforms
   (reproject/align/normalize/calibrate) are offered as auto-applicable;
   fact-gathering actions stay decisions for Pi.
6. **Typed intent documents** (`typedIntentDocument`): required facts (with
   why), optional facts with degradation class, expected products, and
   quality expectations derived from the serving capability knowledge — the
   same layer verification derives from. Surfaced by `harness:preflight` and
   `harness:resolve_intent`.
7. **Observe→verify→repair stays Pi-driven** (`harness:diagnose_run`): the
   tool reads authoritative run state and persisted sidecars, emits
   structured, vocabulary-resolved repair proposals, and enforces a bounded
   per-run diagnose budget with an explicit `stop` document. It never
   executes repairs.
8. **Explainability 2.0**: `harness:explain` adds degradations, resource
   decisions, failure explanation, and reproducibility anchors (run id,
   plan fingerprint, sidecar paths) — read-only projections only.
9. **Eval corpus 9.0**: new closed categories (invalid_input,
   modality_mismatch, recovery, long_plan, cartography, prompt_injection,
   typed_contract);
   prompt-injection cases pin that hostile metadata stays opaque data and
   never changes deterministic control flow.

## Non-goals

No second scheduler/loop; no new registries; no operator/kernel changes;
no help-store edits (Track 10); execution-plane concurrency stays with its
track. Plans compile only to the existing workflow engine.

## Consequences

Consumers of preflight issue actions should switch to
`suggested_action.tool` / `.workbench_command`; the historical `action` key
stays stable. Recipe instantiation output gains additive `degradations`.
The corpus grows (~74 expanded cases), still bounded by the runner cap.
