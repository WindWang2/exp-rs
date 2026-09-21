# Plan — RS14-06 Experiment Debugger / First Divergence Locator

ADR: `docs/adr/0174-experiment-debugger-14.md` (number may be renumbered at union time; see header).
Module: `src/experiment/debugger/` → static lib `sicnu_experiment_debugger` (alias `Sicnu::experiment_debugger`).
Namespace: `sicnu::experiment::debugger`.

## Problem statement

When a student run (or agent run) produces a different result from a reference run — a TA's reference, a replay, or an accepted exemplar — the platform today can only say *whether* the runs differ at whole-run granularity (`RunComparison`, `ReplayDeviationAnalyzer`) and *which pin* differs. It cannot say **where** in the scientific process the two runs first diverged, **what kind** of difference it was, or **how confident** we are that this difference *caused* the outcome divergence. Students see "results differ" and guess; a planner agent has no typed signal to replan from.

## User stories

**本科生 (teaching mode)**
- As a student, after my NDVI→threshold→area experiment finishes with a different area than the reference, I open the debugger and see: dual timelines side by side, the first step that genuinely differs ("rs:threshold_calc: parameters differ — threshold 0.62 vs reference 0.35"), and an honest confidence statement ("high confidence this explains the result difference; evidence: params hash, identical inputs upstream").
- As a student who deliberately used `rs:histogram_equalize` where the reference used `rs:stretch_linear`, the debugger says "alternative valid path (rule `prep-stretch-equivalence` accepted)" and does **not** call my run wrong; it keeps looking and finds my real mistake later (a missed mask step).
- As a student whose run is missing step evidence (single-operator run), I get an honest "insufficient step evidence — run-level comparison only", never a fabricated step story.

**AI Agent (agent mode)**
- As a planner agent, after a failed/failed-to-match run I consume one machine-readable JSON diagnostic (`exp.diag.v1`-aligned): divergence code, kind, step ids, which parameter keys differ, causal confidence, and what evidence would raise confidence — enough to replan without parsing logs or GUI.
- As an agent, when the two runs are not comparable (different dataset identity), I get `non_comparable` with the pin evidence, not a bogus divergence.

## Architecture

```
                ┌────────────────────────────┐
 IRunEvidenceSource (interface)              │ read-only
   ├── ExperimentStoreEvidenceSource ─────────▶ ExperimentStore read APIs
   ├── ProvenanceFileSource ──────────────────▶ provenance_<runId>.json (ProvenanceGraph::fromJson)
   └── InMemoryEvidenceSource (tests)         ▶ (fake)
                          │
                          ▼
              RunSnapshotBuilder.build(runId)
                          │
                          ▼
                  RunSnapshot (value)  ── digest() ──▶ canonical snapshot digest
                          │
        ┌─────────────────┼──────────────────────────┐
        ▼                 ▼                          ▼
   StepAligner      EquivalenceProfile          FirstDivergenceAnalyzer
   (Slice B)        + InvariantSet (Slice D)    (Slice C; uses B and D)
        │                 │                          │
        └────────┬────────┴────────────┬─────────────┘
                 ▼                     ▼
        ArtifactMetricComparer    FirstDivergenceReport (value, JSON v1)
        (Slice E)                     │
                    ┌─────────────────┼──────────────────┐
                    ▼                 ▼                  ▼
            TimelineDiffModel   AgentDiagnosticAdapter  (CLI/GUI wiring = future,
            (Slice F, Qt-free)  (Slice F, exp.diag.v1)   documented in docs/integration.md)
```

Layers and dependency direction:
- `sicnu_experiment_debugger` → `Sicnu::dataset`, `Sicnu::experiment`, `sicnu_workflow` (ProvenanceGraph only), `Qt6::Core`. Mirrors the sanctioned `sicnu_experiment_bridge` link set. Nothing links into it except consumers; no cycles; no global registry; no store writes.
- All comparison logic is **pure projection over recorded evidence**. No execution, no scheduling, no auto-correction, no silent fallback: every "cannot decide" is a typed verdict.

## Public API / data schema

All JSON carries `schema_version: 1` and closed kind strings:
- `exp.debugger.snapshot.v1` — normalized run snapshot.
- `exp.debugger.alignment.v1` — step alignment.
- `exp.debugger.divergence.v1` — first-divergence report.
- `exp.debugger.equivalence.v1` — equivalence profile / invariant set documents.

Core types (all value objects, Qt Core only):
- `RunSnapshot { runId, stepEvidence: absent|provenance_graph|steps_evidence, planSignature, steps[], artifacts[], run pins (algorithm/dataset/split/model/seed/softwareRevision/configHash/resultFingerprint), metrics }` + `snapshotDigest()` — canonical SHA-256 over identity fields only (operatorId, paramsHash, lineageSignature, consumed/produced digests, output digest mode+value, run pins). Volatile fields (elapsed, error text, paths) excluded — same execution ⇒ same digest.
- `StepSnapshot { stepId, originNodeId, operatorId, params, paramsHash, lineageSignature, status, cacheHit?, outputDigest, digestMode, sizeBytes, consumed[], produced[], errorMessage }`.
- `IRunEvidenceSource` — `run(runId)` / `provenanceDocument(runId)`; implementations above; typed absents (`experiment.debugger.evidence_absent`).
- `StepAligner::align(ref, stu, budget)` → `AlignmentResult { matches[{refStepId, stuStepId, kind: exact_id|structural|equivalent_rule|unmatched}], unmatchedReference[], unmatchedStudent[], planRelation }`. Deterministic: same-plan → originNodeId match; different-plan → topological greedy (operator id equal + matched parents + matched root-input digests), ties by step id.
- `FirstDivergenceAnalyzer::analyze(referenceSpec, studentRunId)` → `FirstDivergenceReport` (below).
- `ReferenceSpec` — `{kind: exact_run, runId} | {kind: profiled_run, runId, profile} | {kind: invariants, invariantSet, runId?}`.
- `DivergenceKind` (closed taxonomy): `None | EquivalentAlternativePath | DifferentInputState | MissingPreprocessing | ParameterDivergence | DataSubsetDivergence | GeometryAlignmentDivergence | ResultDivergenceWithoutProcessDivergence | UnknownNonComparable`.
- `DivergenceFinding { kind, stepRef/stepStudent, confidence: high|medium|low|none, evidence[], missingEvidence[], equivalentRuleId }` — **causal confidence is derived only from evidence completeness** (matched parents? digest modes equal? params both present?): high = full upstream matched + direct evidence; medium = direct evidence with partial upstream; low = inferred; none = cannot decide. Never a probability theater.
- `FirstDivergenceReport { verdict: identical|equivalent|divergent|incomplete|non_comparable, firstDivergence?, additionalFindings[](≤64), alignment, runLevelComparison (RunComparison passthrough), artifactComparison, metricDeltas, evidenceGaps[], timeline (Slice F model) }` + `toJson()`.
- `AgentDiagnosticAdapter::forReplan(report)` → `exp.diag.v1`-shaped JSON `{code, component: "experiment.debugger", run, recoverability, suggested_action, cause_chain[], details{divergence_kind, step ids, differing param keys, confidence}}`. Codes (closed set, `experiment.debugger.*` convention): `experiment.debugger.no_divergence`, `.first_divergence`, `.alternative_path_only`, `.insufficient_evidence`, `.non_comparable`, `.unknown_run`, `.evidence_too_large`.

Classification rules (first mismatching position in topological order):
1. Gate: run-level `RunComparison::compare` = NotComparable → verdict `non_comparable`; dataset/split differing ⇒ `DataSubsetDivergence`, else `UnknownNonComparable`. No step walk.
2. Gate: either snapshot has `stepEvidence: absent` → verdict `incomplete`, run-level comparison + metric deltas passthrough, evidenceGaps state exactly what is missing.
3. Root inputs (consumed artifacts with no producer) digest mismatch → `DifferentInputState` (high confidence when both digests present and modes equal).
4. Reference step unmatched in student, downstream consumes its artifact → `MissingPreprocessing`.
5. Student-only step feeding a matched step → the consumer's divergence; classified at the consumer as `DifferentInputState` (evidence names the student-only producer) unless a profile rule accepts it (`EquivalentAlternativePath`).
6. Matched pair, `paramsHash` differ → `ParameterDivergence`; if a profile geometry-keys rule covers the differing keys ⇒ `GeometryAlignmentDivergence` (rule id recorded).
7. Matched pair, process identity equal (paramsHash + lineageSignature + consumed digests), output digest differ → `ResultDivergenceWithoutProcessDivergence` (high when digest modes equal; medium when mixed modes; low when cacheHit states differ).
8. Digest modes incomparable → downgrade confidence + record `missingEvidence`; never false-equal.

Alternative-valid-path handling:
- `EquivalenceProfile` (versioned JSON, `exp.debugger.equivalence.v1`): rules `operator_group` (declared interchangeable operator sets), `param_tolerance` (numeric tolerance per operator key), `commutative_siblings` (order-insensitive siblings), `geometry_keys` (which keys are geometry-affecting for an operator).
- A step matched through a rule records `equivalentRuleId` and kind `EquivalentAlternativePath`; it never terminates the walk; the report verdict becomes `equivalent` only if **no other divergence exists**.
- `InvariantSet`: declared invariants (`metric_within`, `band_count_is`, `no_step_of_operator`, `step_count_at_least`, `final_digest_equals`) evaluated against a snapshot; used as an independent reference when no exact reference run applies. Failures are typed findings, not corrections.

## Migration / compatibility

- Purely additive: one new directory + one `add_subdirectory` line + one test registration block + docs. No existing file's behavior changes (experiment store/workflow headers included read-only).
- No schema of any existing module is touched; all new schemas are versioned and closed-set.
- Rollback / kill-switch: delete the directory + the two registration lines; nothing else depends on it.

## Observability

- Report JSON is the artifact (human + machine). Tracing optional via existing `sicnu_runtime` trace if a consumer wires it — not added in this track (no logging of run contents by default; reports may contain parameter values — params already carry the platform's secret-redaction contract upstream, and snapshots are built from already-recorded, already-redacted evidence).

## Security / trust boundary

- We parse only **platform-produced** provenance/checkpoint JSON via the strict existing parsers (`ProvenanceGraph::fromJson` envelope gates; snapshot `fromJson` does its own strict envelope + version gate). No network, no remote JSON. Depth-bomb issue (#1154/#1155) noted, not touched.
- Report redaction: metrics/params embedded in reports pass through existing `RunEnvironment::redactSecretKeys` before serialization (defense-in-depth, reused not re-implemented).

## Performance budget

- Snapshot: ≤ `maxSteps` (default 4096) steps; exceeding → typed `experiment.debugger.evidence_too_large` (no truncation lies).
- Alignment: same-plan O(N); structural ≤ `maxComparisons` (default 4·10⁶), exceeding → typed budget abort.
- Findings capped at 64; metric deltas capped at 256 leaves (deterministic leaf order, documented cut).
- Digest: single canonical JSON pass; bounded doc size inherited from parser gates.
- No file hashing ever performed by this module (digests are read from evidence; computing digests is the producers' job).

## Test strategy

Catch2 suite `tests/test_experiment_debugger.cpp` (+ fixture builder header `tests/experiment_debugger_fixtures.h`), linked `Sicnu::experiment_debugger Sicnu::experiment Sicnu::dataset SQLite::SQLite3` following the `test_mlops9_evidence` pattern. All in-memory / QTemporaryDir; offline; deterministic.

Per-behavior classes: happy path; invalid/unsafe input (unknown run, malformed provenance envelope, digest mode mismatch); boundary (maxSteps, budget, findings cap); serialization (toJson→fromJson round-trip, byte-stable canonical digest); deterministic replay (same evidence ⇒ same report bytes); compatibility (run-level RunComparison passthrough); teaching/agent semantic consistency (the same evidence produces matching conclusions in both the human timeline model and the agent diagnostic).

Adversarial oracle policy: each fault fixture asserts the **exact** first-divergence step id AND kind AND that upstream steps are all matched-identical — a vacuous "some divergence somewhere" pass is impossible.

## Work packages (slices)

| Slice | Content | Red test first |
|---|---|---|
| A | `RunSnapshot` + digest + `RunSnapshotBuilder` + `IRunEvidenceSource` + store/provenance-file/in-memory sources | snapshot build from synthetic provenance doc; digest equality/stability; unknown run typed error; absent evidence mode |
| B | `StepAligner` same-plan + structural alignment + budget | align same plan; align rewired plan; unmatched honesty; budget abort |
| C | `FirstDivergenceAnalyzer` + taxonomy + confidence + non-comparable/incomplete gates | every taxonomy kind has a fixture pinned to exact step+kind+confidence |
| D | `EquivalenceProfile` + `InvariantSet` + alternative-path walk-through | accepted alternative not misjudged; rule id recorded; invariant failures typed |
| E | `ArtifactMetricComparer` + metric deltas (bounded) | digest compare incl. mode mismatch; metric delta honesty (missing leaf) |
| F | `TimelineDiffModel` + `AgentDiagnosticAdapter` (exp.diag.v1) | model statuses; agent codes; teaching/agent consistency |
| G | Fault-fixture end-to-end validation: band swap, threshold shift, missing mask, grid drift, nondeterministic kernel, incomparable dataset, incomplete evidence + teaching exemplar walkthrough test | each fixture located precisely end-to-end via file-based evidence |

## Integration points (other 19 tracks; detailed in docs/integration.md)

- **RS14-09 planner (#1193)**: consumes `AgentDiagnosticAdapter` output (DTO only, no type dependency).
- **RS14-10 verifier (#1191)**: may consume `FirstDivergenceReport` as explanation evidence; we depend on nothing from it.
- **RS14-17 capsule (#1188)**: capsule export may embed a snapshot digest; no shared code needed now.
- **GUI (future)**: `TimelineDiffModel` is the Qt-free model the workbench panel/dock renders; wiring documented, not implemented.
- **CLI (future)**: `experiment debug` command sketch documented, not implemented (avoids central-file churn in this track).

## Definition of Done

1. All slices A–G green; every taxonomy kind exercised by a fixture pinned to exact step + kind + confidence.
2. Public DoD of the campaign (two review rounds, dedup re-check, targeted regression, no new warnings).
3. Track DoD: teaching e2e exemplar test; machine-readable agent diagnostic test; single-source-of-truth audit (no second digest/provenance/store — verified by grep in review); typed results for unsafe/unknown/unsupported; offline tests; bounded budgets; versioned schemas; docs (ADR + docs/experiments/debugging.md + docs/integration.md).
