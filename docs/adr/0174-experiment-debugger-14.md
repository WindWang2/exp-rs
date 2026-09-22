# ADR 0174: Experiment Debugger — First-Divergence Localization over Recorded Run Evidence

- Status: Accepted (RS14 Track 06, 2026-09-22)
- Track: `RS14-06-experiment-debugger`
- Module: `src/experiment/debugger/` (`sicnu_experiment_debugger`)
- Renumber note: ADR numbers 0172/0173 were claimed by concurrently-in-flight PRs (#1191, #1192).
  If the union merge collides, renumber this file and update the two references in
  `docs/experiments/debugging.md` — the module code carries no ADR-number dependency.

## Context

The platform can already answer **whether** two recorded runs differ and **which identity pin**
differs: `RunComparison` (ADR 0137), `ReplayDeviationAnalyzer` (M7), `RepeatExecutionClassifier`,
`pairedRunComparison` (comparison_ext), `compareBenchmarkResults` (D19). All operate at WHOLE-RUN
granularity. Per-step evidence exists in three recorded forms — checkpoint step plans
(`StepPlan`), the D17 provenance graph (`provenance_<runId>.json`), and the bridge's
`run.metrics()["workflow"]["steps"]` summaries — but **no module joins step-level records across
two runs**, and nothing anywhere locates a FIRST divergence.

Consequences today: a student whose experiment result differs from the reference sees only
"results differ"; a planner agent has no typed signal to replan from. Teachers cannot explain
WHERE the student's process went wrong without manually diffing logs.

## Decision

1. **Read-only projection, new leaf library.** `sicnu_experiment_debugger` links
   `Sicnu::experiment` (store reads) + `sicnu_workflow` (strict checkpoint loader,
   provenance-graph parser) — the same sanctioned combination as `sicnu_experiment_bridge`.
   Nothing links into it except opt-in consumers; the science core never links back; the layer
   guard keeps it GUI- and network-free.

2. **One normalized snapshot value (`RunSnapshot`)** joins the run record with the richest
   available step evidence (checkpoint > provenance graph > bridge summaries > absent), exposing
   per-step operator identity, canonical parameter hash, lineage signature, output digest (mode-
   tagged), dependencies, and root-input artifacts. A canonical digest (`snapshotDigest`) covers
   identity only; volatile fields (timings, paths, sizes, error text, status labels) are excluded
   so the same execution always yields the same digest.

3. **Deterministic alignment (`StepAligner`)**: same plan signature → exact-id matching;
   different plans → structural matching in topological order (operator equality, matched-parent
   correspondence both directions, content-digest preference), with partial parent coverage
   flagged (`parentCoverageComplete=false`) instead of hidden. Budget-bounded; typed abort.

4. **Closed divergence taxonomy with causal confidence (`FirstDivergenceAnalyzer`)**: run-level
   comparability gate first (reusing `RunComparison` — the existing single truth), then a single
   topological walk classifying: different input state / missing preprocessing / parameter
   divergence / data subset divergence / geometry-alignment divergence / result divergence
   without process divergence / unknown-non-comparable. Confidence (none/low/medium/high) is
   derived ONLY from evidence completeness — upstream verification, digest presence and mode
   agreement, parameter availability. Digest-mode mismatches are incomparable, never false-equal.

5. **Alternative-valid paths require DECLARED rules (`EquivalenceProfile`)**: operator groups,
   parameter tolerances, geometry keys. Every accepted difference names the rule that accepted
   it; profiles can accept differences but never create or hide them. Invariant sets
   (`metric_within`, `no_step_of_operator`, `step_count_at_least`, `final_digest_equals`) serve
   as references when no exact reference run exists. **Sibling-order differences are NOT a rule
   kind** — they are absorbed structurally by topological normalization + content-digest
   matching, and genuinely rewired dataflow is a scientific difference that must be reported.

6. **Qt-free surfaces for humans and agents**: `TimelineDiffModel` (dual-run timeline with the
   first divergence flagged — GUI wiring is a future consumer, not part of this ADR) and
   `AgentDiagnosticAdapter` emitting the platform's `exp.diag.v1` envelope (codes verbatim from
   the closed `experiment.debugger.*` set, recoverability + suggested_action + cause chain) with
   a closed `details` extension (divergence kind, confidence, step ids, differing keys). Teaching
   and agent views are derived from the SAME report and cannot disagree.

## Honest-degradation ladder

Rich evidence missing ⇒ the report SAYS so: `incomplete` (named gaps) when either run lacks step
evidence; `non_comparable` when identity pins differ (dataset/split/model); `UnknownNonComparable`
findings when digest modes cannot be mixed or parameters were not recorded. Nothing is guessed,
zero-filled, or silently repaired.

## Consequences

- Consumers: tests now; CLI (`experiment debug`), workbench panel button, and MCP surface are
  documented integration points (`docs/integration.md`), deliberately NOT wired here to keep the
  central-file delta minimal for the union merge.
- Budgets: ≤4096 steps/snapshot, ≤8192 artifacts, ≤4·10⁶ alignment comparisons, ≤64 findings,
  ≤256 metric leaves, 16 MiB evidence-file read cap — every overflow typed, never truncated.
- Risks: the checkpoint/provenance area is actively moving; this module consumes only their
  strict read contracts. `Experiment.runIds` (issue #1172) is never consulted; reference/student
  ids are explicit inputs.
- Out of scope (stays with their own tracks): all issues listed in the recon avoidance matrix;
  recording/writing of runs; any GUI implementation.

## Verification

`tests/test_experiment_debugger.cpp` — per-slice contract suites with adversarial oracles (exact
step id + kind + confidence per fault fixture; mixed-mode, budget, cap, envelope-rejection,
deterministic-replay cases). End-to-end fault localization: band swap, threshold shift, missing
mask, grid drift, nondeterministic kernel, incomparable dataset, incomplete evidence, accepted
alternative path.
