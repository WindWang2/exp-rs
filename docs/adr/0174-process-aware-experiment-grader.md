# ADR 0174 — Process-aware Experiment Grader

Status: Accepted (track RS14-05-process-grader) · Date: 2026-09-22
Module: `src/grader/` (target `sicnu_grader`) · Test lane: `ctest -R "test_grader::" -j1`

## Context

Existing grading is artifact-scoped: the D4 teaching grader (`src/agent/output_verifier.h`,
ADR 0150) asserts on final rasters under `sicnu.lab.rules/1` and explicitly refuses partial
credit. Experiment *processes* — workflow stage transitions, provenance, checkpoints, metric
documents, student answers — accumulate rich evidence that no component grades. Meanwhile
three sibling RS14 tracks draw adjacent territory: RS14-10 (`src/verify/`) judges *contracts*
(pass/indeterminate/fail, never scores), RS14-14 (`src/agentbench/`) grades agent
*trajectories* from recorded traces, RS14-17 (`src/experiment/capsule/`) projects runs into
portable capsules. `LabGradeEmbedding` (`src/experiment/bridge/lab_report.h`) reserves a typed
slot for a grade document that nothing fills.

## Decision

Add a **pure C++20 + jsoncpp leaf library** that grades experiment *processes* against a
teacher-authored, versioned rubric, producing a deterministic, digest-bound report with a
reason chain on every non-full outcome.

1. **Three versioned documents** (readers refuse foreign versions, fail closed):
   `sicnu.grader.rubric/1`, `sicnu.grader.evidence/1`, `sicnu.grader.report/1`.
2. **Evidence, not buttons.** The grader consumes recorded facts. No schema member represents
   UI action order; the only sequencing a rubric can declare is `orderedAfter` between
   *stages* (state transitions), enforced only where declared.
3. **Partial credit, teacher-bounded.** Metric criteria may declare tolerance bands and an
   optional linear window; answer criteria carry teacher-authored concept points. The engine
   never invents a curve.
4. **Reason chain.** Every criterion that did not earn full points carries ≥1 machine-readable
   reason slug (`grader:<slug>`, append-only) plus the evidenceIds it was judged against.
   Evidence-less scoring is a P0 defect (inherited from ADR 0150).
5. **Hard constraints** (forbidden/required evidence) cap or zero the report and are themselves
   evidence-cited. **Alternate pathways** are named criterion sets; the report names which
   matched, chosen deterministically (highest earned, tie → pathwayId lexicographic).
6. **Fail closed at the document level; indeterminate at the judgment level.** Invalid
   documents (foreign version, duplicate ids, weight-sum mismatch, dangling refs, budget
   overrun, non-finite numbers) produce *no report*, only typed `grader:e-*` errors. Missing
   or ambiguous evidence is *not* an error: it becomes `not_earned`/`indeterminate` outcomes
   with reasons — never a silent zero.
7. **Determinism.** Reports carry no wall clock; digest = SHA-256 over the canonical body
   (sorted members, shortest round-trip numbers). Double-grading is byte-identical.
8. **Leaf posture.** Zero Qt/GDAL/sicnu dependencies; no I/O; light Catch2 lane. Store /
   provenance / checkpoint / lab adapters are the caller's integration job (documented in
   `docs/integration.md`); the module ships pure JSON→JSON adapters for the *document shapes*
   only. Metric formulas stay in `src/experiment/evaluation.*` (the grader reads values, it
   never recomputes formulas); artifact assertions stay in D4 (`artifact_grade` evidence is
   consumed, not produced); run-identity stays in `RepeatExecutionClassifier`; contract
   verdicts stay in RS14-10 `src/verify/` (`verifier_verdict` evidence is consumed via
   provider shape, not linked — that module is not merged yet).

## Consequences

- Grading semantics live in exactly one place (`src/grader`); no second registry, store, or
  provenance implementation.
- Budgets (≤4096 criteria hard cap, ≤100k evidence items hard cap, 64 KiB per answer) are
  typed refusals — a hostile document cannot make grading unbounded.
- `LabGradeEmbedding.inlineResult` gains a natural future filler (the report document); the
  same machine-readable report is the reserved seam for Agent outcome verification loops
  (not built in this track).
- Number canonicalization is a jsoncpp-leaf form (sorted members, shortest round-trip
  doubles); it is intentionally *not* the Qt-side `canonicalizeJsonRfc8785`, which is out of
  reach for a Qt-free library.
- ADR numbering: 0172 is shared by the capsule and verifier tracks (accepted repo practice:
  0130×6, 0148×4), 0173 by the capability-graph track; this track takes 0174.
