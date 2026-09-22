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

### Slice C — GREEN (2026-09-22)
- FirstDivergenceAnalyzer: run-level RunComparison gate → identity shortcut → alignment →
  single topological walk → typed findings with honest confidence; findings cap; deterministic
  byte-stable report.
- SEMANTIC DECISION (from a failing test): accepted-alternative findings when the verdict is
  "equivalent" live in additionalFindings only — firstDivergence stays empty and
  hasFirstDivergence=false. A "first divergence" that is not a divergence was a contradiction.
- Two fixture bugs found by the red-green cycle (tests were self-inconsistent): mixed-modes test
  wrote equal digest modes with prefixed digests (RDWPD was the CORRECT verdict); missing-mask and
  parameter fixtures had physically impossible equal downstream digests. Fixed fixtures; the
  immaterial-missing-step (Low confidence) path remains reachable for genuinely equal outputs.
- 26/26 pass (188 assertions) incl. A+B regression.

### Slice D — GREEN (2026-09-22)
- EquivalenceProfile (operator_group / param_tolerance / geometry_keys; strict versioned parsing,
  unique rule ids) + InvariantSet (metric_within / no_step_of_operator / step_count_at_least /
  final_digest_equals) + profile-aware aligner/analyzer overloads.
- DESIGN NOTE: commutative_siblings was dropped during implementation analysis — pure order
  differences are structurally absorbed by topological normalization + content-digest matching;
  genuinely rewired dataflow must be reported, not blessed. Documented in equivalence.h + ADR.
- Profile semantics settled by tests: a profile-accepted difference yields verdict "equivalent"
  with a NAMED finding (rule id); firstDivergence stays empty (consistent with Slice C).
- Analyzer now aligns through the profile overload when one is given (caught by the operator-group
  test: the plain aligner left the rule-matched step unmatched → phantom missing_preprocessing).
- Test-side: QVector::operator<< mutation polluted later assertions — explicit copy.
- 32/32 pass (236 assertions) incl. A+B+C regression.

### Slices E + F — GREEN (2026-09-22)
- ArtifactMetricComparer: matched-pair digest verdicts (equal/different/one_sided/
  incomparable_modes/both_absent) + bounded sorted-path metric deltas via metricValueAtPath.
- TimelineDiffModel (Qt-free) + AgentDiagnosticAdapter (exp.diag.v1 codes, recoverability,
  suggested_action, details{kind,confidence,step ids}); teaching/agent consistency test pins that
  both views name the SAME divergence step.
- Test-side fix: a one-sided metric leaf was asserted as reference-present — contradicted the
  no-zero-filling contract; the test now pins the honest one-sided shape.
- 38/38 pass (295 assertions).

### Slice G — GREEN (2026-09-22)
- End-to-end through REAL recorded evidence: WorkflowCheckpointManager::saveCheckpoint writes
  production checkpoints; a real SQLite ExperimentStore holds the run records;
  DirectoryEvidenceSource reads them back through the strict loader.
- Fault fixtures all locate the exact first divergence: threshold shift (ParameterDivergence,
  high), missing mask (MissingPreprocessing at consumer, high), nondeterministic kernel
  (ResultDivergenceWithoutProcessDivergence, high), different scene (non_comparable +
  DataSubsetDivergence), missing checkpoint (incomplete, named gap), within-window threshold
  (equivalent + named tolerance rule), and the teaching exemplar (timeline flag == agent
  diagnostic step; report persisted and re-parses under exp.debugger.divergence.v1).
- Store-contract discoveries (behavior confirmed correct, tests adapted): runs require their
  parent experiment (experiment.not_found); pin rewrites on upsert are refused — the dataset
  fixture inserts the differing pin from the start.
- 45/45 pass (410 assertions) — full suite green.

### Deep review round 1 — FIX-FIRST verdict, all findings resolved (2026-09-22)
Independent adversarial review (read all 32 changed files, built the branch, empirically
reproduced findings #1 and #2). Findings and resolutions:
- P0 #1 NotComparable gate tested detail-emptiness (always "identical") ⇒ every non-dataset
  cause reported as data_subset_divergence/high. Fixed: test RunDiffItem.differs; new
  model-only pin test pins UnknownNonComparable.
- P1 #2 provenance normalizer was edge-order-dependent (writer sorts by from,to,to kind);
  consumers sorting before producers lost dependencies. Fixed: two-pass resolution; new test
  with writer-ordered edges.
- P1 #3 findings cap could flip verdict divergent→equivalent. Fixed: first substantive finding
  secured before capping; only the additional list is bounded; cuts named in evidenceGaps;
  new cap test.
- P1 #4 RDWPD High without any verified process dimension. Fixed: High requires
  recorded-and-equal params or lineage + comparable cache + verified upstream; missing
  dimensions named; new bridge-mode test.
- P2 #5 MissingPreprocessing tri-state confidence (Unknown no longer High).
- P2 #6 root handling: fires at any ref root consumer; incomparable recorded roots add a named
  gap; identityDocument roots are content-identity (digest+mode, sorted by identity) consistent
  with the analyzer.
- P2 #7 timeline keep-first: co-located additional findings can no longer repaint the
  first-divergence entry.
- P2 #8 canonicalParamsHash replaced by sicnu::experiment::runConfigHash (SSOT).
- P2 #9 ADR/debugging.md/integration.md statements aligned with the code (correspondence
  direction, real evidence strings, analyze() signature, capsule stamp = identityDocument()).
- P2 #10 evidence ladder: one policy — every stage failure degrades openly with FULL diagnostic
  propagation; hard failure only when nothing succeeds.
- P3 cluster: agent-diagnostic comment truth + missing-step fallback + evidence-absent mapping
  (insufficient_evidence/manual); invariant findings capped, ledger carries its own schema kind;
  bridge steps_truncated surfaced as a warning (propagated by the builder); duplicate artifact
  ids rejected; children map hoisting noted (bounded U·V, nodes ≤4096 — accepted); test cruft
  removed; jsonCppToQJson null-drop documented.
- Deprecation warning in fixtures removed (zero new warnings).
- Suites: test_experiment_debugger 49/49 (438), test_experiment_evaluation 20/20 (299),
  test_mlops9_evidence 4/4 (94).

### Deep review round 2 — SHIP verdict (2026-09-22)
Independent re-review verified every round-1 fix against the pre-fix defect (not just the commit
message) and confirmed the four new potency tests fail on pre-fix code. All P0/P1 RESOLVED, all
P2 RESOLVED, zero warnings in debugger TUs, 49/49 (438 assertions) green.
Residuals from round 2 cleaned in the follow-up commit: debugging.md trailing comma (invalid
example JSON), equivalence.cpp comment/code mismatch + stale Q_UNUSED(options), builder comment
overstatement about warning reach. Round-2 edge observations (P4 rootVerify report-once
interaction; truncation warnings stopping at snapshot diagnostics) documented as known limits
below and in the ADR.

### Known limits (documented, not defects)
- A report-once root-input finding lets later root-consuming pairs treat upstream as verified;
  the input-state finding still wins firstDivergence by position.
- Bridge truncation warnings surface on the snapshot Result diagnostics; surfacing them inside
  FirstDivergenceReport.evidenceGaps requires an API that carries diagnostics into analyze() —
  deferred as an integration-point decision.
- ADR 0174 number is contested by concurrent PRs #1194/#1196/#1197 — renumber at union per the
  ADR header note.
