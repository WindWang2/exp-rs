# Integration Notes — RS14-06 Experiment Debugger

This track ships a self-contained read-only library plus docs. It deliberately does NOT wire
into CLI/GUI/MCP to keep the central-file delta minimal for the RS14 union merge. The seams are
stable value objects; wiring is mechanical.

## What exists now

- `src/experiment/debugger/` — `sicnu_experiment_debugger` (alias `Sicnu::experiment_debugger`),
  links `Qt6::Core + Sicnu::dataset + Sicnu::experiment + sicnu_workflow` (same combination as
  `sicnu_experiment_bridge`). Layer guard: no GUI, no network.
- `tests/test_experiment_debugger.cpp` — per-slice contract suites.
- `docs/adr/0174-experiment-debugger-14.md`, `docs/experiments/debugging.md`.

## Wiring points (future tracks; no code exists here for these)

### CLI (`src/cli/cli_dataset_commands.cpp`)
Sketch: `experiment debug --reference <runId> --student <runId> [--dir <runDirectory>]
[--profile <profile.json>]` —
1. `ExperimentStore store; store.open(dbPath)` read-only;
2. `DirectoryEvidenceSource source(&store, runDir);`
3. `RunSnapshotBuilder b(source); auto r = b.build(ref); auto s = b.build(stu);`
4. `FirstDivergenceAnalyzer::analyze(refRun, stuRun, refSnap, stuSnap)` — signature order
   `(referenceRun, studentRun, referenceSnapshot, studentSnapshot)`; the profile overload appends
   `const EquivalenceProfile&` before the options;
5. print `report.toJson()` (already human-readable), exit nonzero when `verdict == "divergent"`.

### GUI (workbench `dataset_experiment_panel`)
The panel's existing `compareSelectedRuns()` opens the whole-run `RunComparison`. Add a
"first divergence" action: build the two snapshots via the panel's store + run directory and
render `TimelineDiffModel::build(...)` — a pure value model; a list/tree widget suffices. The
model is Qt-free by design; the GUI owns presentation only.

### MCP / agent surface
`AgentDiagnosticAdapter::forReplan(report)` returns the `exp.diag.v1`-aligned object
(`experiment.debugger.*` codes). A `debug_run` tool would wrap: inputs
`reference_run_id, student_run_id, profile?`, output = `AgentDiagnostic.toJson()` +
`FirstDivergenceReport.toJson()` as the tool's structured payload. No new taxonomy: codes ride
the existing diagnostic vocabulary and the `diagnostic_catalog` can index them via
`data/help/diagnostics.json` entries (additive).

### RS14-09 Scientific Task Planner (PR #1193)
Consume `AgentDiagnostic` as a DTO after a failed/rejected plan execution: `code` +
`details.divergence_kind` + `details.confidence` are the replan signal; `suggested_action` is
advisory text. No type dependency — copy the struct or parse the JSON.

### RS14-10 Unified Scientific Verifier (PR #1191)
A `FirstDivergenceReport` is admissible EXPLANATION evidence for a verifier's reject verdict
(run-level `non_comparable` / `divergent` verdicts already distinguish "wrong data" from "wrong
process"). The verifier may read reports produced elsewhere; the debugger never calls the
verifier.

### RS14-17 Reproducibility Capsule (PR #1188)
A capsule that wants a run-content stamp should embed the canonical
`RunSnapshot::identityDocument()` (or its hash) — `snapshotDigest()` additionally covers the
run id, so two records of the same execution compare equal via the identity document, not the
per-run digest. Both are versioned (`exp.debugger.snapshot.v1`); capsule-side validation
compares these, it does not recompute evidence.

## Evidence directory convention

`DirectoryEvidenceSource(store, runDirectory)` expects the platform's recorded layout:
`checkpoint_<runId>.json` and/or `provenance_<runId>.json` in the run directory root or in
`attempt-<N>/` subdirectories (highest attempt preferred). This matches
`PipelineRunCoordinator`/`WorkflowCheckpointManager` writers. The store is optional (pass
nullptr for evidence-directory-only use).

## Versioning

All emitted documents are schema-gated: `exp.debugger.snapshot.v1`,
`exp.debugger.divergence.v1`, `exp.debugger.equivalence.v1`, `exp.debugger.invariants.v1`
(`schema_version: 1`). Unknown kinds/versions are refused, never reinterpreted.
