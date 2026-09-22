# Experiment Debugging — First-Divergence Localization (RS14-06, ADR 0174)

When an experiment run finishes with a different result than a reference — a TA's exemplar, a
replay, an accepted lab path — the debugger explains **where the two recorded processes first
diverged**, **what kind of difference it is**, and **how confident** the platform is that this
difference caused the outcome difference.

The debugger is a **read-only projection**: it reads the experiment store and the workflow
checkpoint / provenance snapshots your runs already produced. It executes nothing, writes
nothing, and never silently repairs scientific state.

## The teaching walkthrough (Undergraduate Lab: NDVI → Threshold → Area)

A lab asks students to compute a vegetation area from a Sentinel-2 scene:

1. `rs:ndvi` — vegetation index from the raw scene
2. `rs:threshold_calc` — binarize with threshold `0.35`
3. `rs:area_stats` — area of the binary mask

The student submits; the platform records the workflow run (checkpoint with per-step plans) and
the experiment run (identity pins + metrics). The TA's reference run is recorded the same way.

The student's area is **twice** the reference area. Instead of staring at two numbers:

**Without step evidence** (single-operator runs recorded without the bridge), the debugger
answers honestly: `incomplete` — "reference run carries no step evidence" — plus the whole-run
pin comparison. It will not invent a step story.

**With recorded step evidence**, the debugger reports:

```json
{
  "kind": "exp.debugger.divergence.v1",
  "verdict": "divergent",
  "has_first_divergence": true,
  "first_divergence": {
    "kind": "parameter_divergence",
    "reference_step_id": "threshold",
    "student_step_id": "threshold",
    "confidence": "high",
    "evidence": [
      "params_hash: reference=9f1a… student=44be…"
    ],
    "missing_evidence": [],
  }
}
```

Reading it as a class: both runs built the same NDVI (verified identical), and the FIRST thing
that differs is the **threshold parameter** at step `threshold` — with **high** confidence,
because everything upstream was verified identical. The downstream area difference is then a
*consequence*, listed as an additional finding, not a second mystery.

### What the confidence means (and does not mean)

| Confidence | Meaning |
|---|---|
| `high` | Direct evidence (parameters/digests recorded) AND every upstream step verified identical (for a result-without-process divergence: at least one process dimension verified equal, cache state comparable, upstream verified) |
| `medium` | Direct evidence, partial upstream verification (e.g. cache-served step) |
| `low` | Indirect evidence only (e.g. lineage signature differs but parameters were not recorded) |
| `none` | The evidence cannot decide — this is a named gap, never a guess |

### Deliberately different but correct paths

A student who used `rs:histogram_equalize` where the reference used `rs:stretch_linear` is not
wrong if the lab declares those interchangeable. The lab's **equivalence profile** declares it:

```json
{
  "kind": "exp.debugger.equivalence.v1", "schema_version": 1,
  "profile_id": "prep-stretch-equivalence",
  "rules": [ { "kind": "operator_group", "rule_id": "stretch-group",
               "operators": ["rs:stretch_linear", "rs:histogram_equalize"] } ]
}
```

With the profile active, that difference is recorded as `equivalent_alternative_path` naming the
rule — and the walk continues to the student's REAL mistake (say, a skipped mask step), which is
still found and reported. Differences nobody declared are never silently blessed; the
`alternative_path_only` verdict lists what a teacher may want to formalize.

## The agent contract (machine-readable)

After an agent run, the planner consumes one diagnostic aligned to the platform's
`exp.diag.v1` envelope — codes, no prose-mining:

```json
{
  "schema": "exp.diag.v1",
  "code": "experiment.debugger.first_divergence",
  "component": "experiment.debugger",
  "student_run_id": "run-stu",
  "reference_run_id": "run-ref",
  "recoverability": "manual",
  "suggested_action": "reconcile the differing parameters at step 'threshold' with the reference, or declare a tolerance rule if the difference is intended",
  "details": {
    "verdict": "divergent",
    "divergence_kind": "parameter_divergence",
    "confidence": "high",
    "reference_step_id": "threshold",
    "student_step_id": "threshold"
  }
}
```

Codes (closed set): `no_divergence`, `first_divergence`, `alternative_path_only`,
`insufficient_evidence`, `non_comparable`, `unknown_run`, `evidence_too_large`,
`malformed_evidence`. The full typed report (`exp.debugger.divergence.v1`) rides alongside for
agents that want the evidence lists.

## Where the evidence comes from

| Mode | Source | Carries |
|---|---|---|
| `checkpoint_steps` | `checkpoint_<runId>.json` step plans | parameters, lineage fingerprint, output digest, dependencies |
| `provenance_graph` | `provenance_<runId>.json` | lineage signature, artifact fingerprints, root inputs, plan signature |
| `steps_evidence` | `run.metrics()["workflow"]["steps"]` | id/operator/status/output digest |
| `absent` | — | run-level pins only |

Richer modes win. Fewer recorded facts ⇒ lower confidence and named gaps — by construction.

## Budgets (bounded by contract)

4096 steps / 8192 artifacts per snapshot · 4·10⁶ alignment comparisons · 64 findings · 256 metric
leaves · 16 MiB per evidence file. Exceeding a budget is a typed result
(`experiment.debugger.evidence_too_large`, `alignment_budget_exceeded`) — never a truncation lie.
