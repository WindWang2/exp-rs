# Progress — RS14-06 Experiment Debugger

## 2026-09-22

### Phase 0–2 complete
- `recon.md`, `plan.md`, `slices.md` committed (4f76a19a1).
- Dynamic dedup: 6 open PRs (#1188–#1193) — no overlap; issue-avoidance matrix recorded.
- Branch `agent/rs14-experiment-debugger`, worktree `../exp-rs-wt-rs14-exp-debugger`, baseline `4f6632e1f`.

### Slice A — RED round
- Wrote contract tests first: `tests/test_experiment_debugger.cpp` (9 Slice A cases) + `tests/experiment_debugger_fixtures.h`.
- Wrote compiling stubs whose capability bodies are intentionally missing:
  - `RunSnapshotBuilder::build` — joins run record only, always `Absent` steps;
  - `stepEvidenceFromProvenanceDoc` / `stepEvidenceFromBridgeWorkflowMetrics` — typed failures;
  - `DirectoryEvidenceSource::steps` — typed absent;
  - `RunSnapshot::snapshotDigest` — hashes run_id only (digest semantics absent).
- Expected RED (capability missing, not fixture breakage): provenance normalization, digest
  identity/volatility semantics, checkpoint paramHash, absent-mode pins projection, oversized cap.
  Expected already-green plumbing (documented honestly): unknown-run typed failure (propagation
  contract), JSON envelope rejection + round-trip (serialization contract written with the types).
- First build = one-time full dependency configure+compile (`build-dev`, `-j1`).

### Slice A — GREEN (2026-09-22)
- Applied GREEN: provenance/bridge normalizers (shared production functions), checkpoint
  conversion, DirectoryEvidenceSource ladder (checkpoint → provenance → bridge → absent),
  RunSnapshotBuilder (paramsHash, pins, caps), identityDocument()/snapshotDigest() single truth.
- Fixture bugs found BY the red-green cycle (the tests were right, fixtures were wrong):
  1. QJson value-copy trap: "nodes" inserted before artifact nodes were appended; also "edges"
     was never inserted at all. Fixed by inserting both arrays only after full assembly.
  2. Terminal run fixture lacked finishedAtUtc — ExperimentRun::fromJson correctly refused.
- 9/9 pass (61 assertions). Note: ctest discovery needs LD_LIBRARY_PATH with the SDK lib dir
  (libodbc); direct binary execution used for the loop.
- Commit: slice A.

### Slice B — GREEN (2026-09-22)
- StepAligner: same-signature exact-id pass; structural pass in topological order with
  content-digest preference, budget counter, single-writer unmatched lists.
- DESIGN CORRECTION (adversarial finding during RED-GREEN): bidirectional matched-parent
  correspondence destroys missing-preprocessing localization (the consumer stops matching the
  moment its producer set differs). Final rule: reference-side matched-parent consistency only;
  student-side extra/absent producers are accepted but flagged via parentCoverageComplete=false.
  Documented in code; Slice C coverage analysis consumes the flag.
- Staging-helper bug fixed (chain-shape coverage test didn't express the mask scenario; rewritten
  to the real mask topology).
- 7/7 pass (53 assertions); Slice A regression 9/9.
