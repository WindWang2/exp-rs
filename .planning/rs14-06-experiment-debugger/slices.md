# Slices — RS14-06 Experiment Debugger

TDD contract per slice: RED (failing test proving the capability is missing) → GREEN (minimal) → REFACTOR → narrow test run → adversarial pass → commit → progress.md update.

Test target: `tests/test_experiment_debugger.cpp` (single executable, grows per slice; fixtures in `tests/experiment_debugger_fixtures.h`).
Run command: `ctest -R test_experiment_debugger -j1` after building that single target with `-j1`.

## Slice A — normalized run snapshot + digest
Files: `src/experiment/debugger/run_snapshot.{h,cpp}`, `evidence_source.{h,cpp}`, `snapshot_builder.{h,cpp}`, `debugger_types.h` (codes/kinds), CMake.
RED tests:
1. build from synthetic `d17_provenance` doc → steps in topological order, artifacts joined, `stepEvidence=provenance_graph`, planSignature captured.
2. `snapshotDigest()` equal for two docs differing only in key order/whitespace of params; differs when one param value differs (not conflated with result digest).
3. unknown runId → typed error `experiment.debugger.unknown_run` (no throw).
4. run with only bridge step evidence (`run.metrics()["workflow"]["steps"]`) → `stepEvidence=steps_evidence`.
5. run with neither → `stepEvidence=absent` (valid snapshot, honest).
6. steps beyond maxSteps → `experiment.debugger.evidence_too_large`.
7. toJson/fromJson round-trip preserves digest; strict envelope `exp.debugger.snapshot.v1` rejects foreign kinds.
Commit: `feat(debugger): slice A — normalized run snapshot + canonical digest (RS14-06)`

## Slice B — step matching / graph alignment
Files: `step_aligner.{h,cpp}`.
RED: same-plan signature → exact_id matches; rewired plan (same operators, different parents) → structural matches with honest unmatched; student-extra / ref-only steps listed; budget abort typed `experiment.debugger.alignment_budget_exceeded`; determinism (same input → same match vector).
Commit: `feat(debugger): slice B — deterministic step alignment (RS14-06)`

## Slice C — first divergence classification
Files: `first_divergence.{h,cpp}`.
RED: fixtures pin exact (step, kind, confidence) for: root-input state diff → DifferentInputState/high; params differ → ParameterDivergence/high; missing ref step consumed downstream → MissingPreprocessing/high; dataset pin diff → non_comparable + DataSubsetDivergence (no walk); absent evidence → incomplete + evidenceGaps; process equal + output digest differ → ResultDivergenceWithoutProcessDivergence (high/medium by digest mode); digest mode mismatch → confidence downgrade + missingEvidence; determinism (byte-stable toJson); findings cap.
Commit: `feat(debugger): slice C — first-divergence taxonomy + causal confidence (RS14-06)`

## Slice D — alternative-valid-path handling
Files: `equivalence.{h,cpp}`.
RED: operator_group rule → match kind equivalent_rule + rule id recorded, walk continues past it; real divergence still found after accepted alternative; profile-less comparison of different operators → divergence (no silent equivalence); param_tolerance rule boundary (within/outside); commutative_siblings order tolerance; invariant-set reference: passing/failing invariants typed; profile document versioned + strict parse.
Commit: `feat(debugger): slice D — equivalence profiles + invariant references (RS14-06)`

## Slice E — artifact metric comparison
Files: `artifact_metrics.{h,cpp}`.
RED: divergence-step artifact digest compare (equal/differ/mode-mismatch → typed unknown, not false-equal); final metric leaf deltas via `metricValueAtPath` (present both / one-sided → honest missing marker); leaf cap; determinism.
Commit: `feat(debugger): slice E — artifact + metric comparison at divergence (RS14-06)`

## Slice F — UI diff model + agent adapter
Files: `timeline_model.{h,cpp}`, `agent_diagnostic.{h,cpp}`.
RED: timeline entries in topological order with per-entry status identical|equivalent|divergent|missing_ref|extra_student|unknown; first divergence flagged; agent diagnostic codes (closed set) per verdict; suggested_action per kind; teaching/agent consistency: same evidence ⇒ timeline's first divergent entry == agent diagnostic step; JSON shape carries `code`, `component`, `recoverability`, `suggested_action`, `cause_chain`.
Commit: `feat(debugger): slice F — Qt-free timeline model + exp.diag.v1 agent adapter (RS14-06)`

## Slice G — fault fixtures end-to-end + teaching exemplar
Files: fixtures header extended; `tests/test_experiment_debugger.cpp` e2e section; `docs/experiments/debugging.md` exemplar walkthrough.
RED (written against the *public analyze* API only, file-based evidence via QTemporaryDir laid out like a lab run dir): band swap (band-selection param keys), threshold shift, missing mask step, grid drift (geometry profile), nondeterministic kernel (result divergence without process divergence), accepted alternative path + later real error, incomparable dataset, incomplete evidence. Each asserts exact first-divergence step + kind + upstream-all-identical.
Commit: `feat(debugger): slice G — end-to-end fault localization fixtures + teaching exemplar (RS14-06)`

## Docs & ADR (with Slice G commit or immediately after)
- `docs/adr/0174-experiment-debugger-14.md`
- `docs/experiments/debugging.md` (student-facing walkthrough + agent contract)
- `docs/integration.md` (planner/verifier/capsule/GUI/CLI wiring points)
Commit: `docs(debugger): ADR 0174 + debugging guide + integration notes (RS14-06)`

## Resource discipline
- Single new test executable; `cmake --build ... --target test_experiment_debugger -j1`; `ctest -R '^test_experiment_debugger$' -j1`.
- Full framework compile is NOT required for slice iteration: the lib is Qt-Core-only + existing libs.
- One broader targeted regression (`ctest -R 'experiment|provenance' -j1`) after Slice C and before PR only.
