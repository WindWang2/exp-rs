# Triage: dataset + experiment issues (#1546 #1547 #1580-#1586 #1635)

Worktree HEAD: `ad98a7a704` (merge of PR #1677). Issue baseline was `2d2881631f`.
Fix commits verified: `831f8d6006` "fix(src/experiment): resolve full-review findings #1546 #1547 #1580 #1581 #1582 #1635" and `c8ba519901` "fix(src/dataset): resolve full-review findings #1583 #1584 #1585 #1586" (both cited below via `git log -1 -S'<marker>' -- <file>`). Later commit `388f7db02f` (R21-B) only added A5 orphan-metric compensation to the importer; it did not regress any fix.

## Verdict table

| Issue | Area | Verdict | Fix commit | One-line evidence |
|---|---|---|---|---|
| #1546 | experiment/repeat_execution | FIXED_OPEN | 831f8d6006 | Shadowing `int unreadableByRef` declaration deleted; guard at :252 now reads the counter incremented at :225 |
| #1547 | experiment/replay_deviation | FIXED_OPEN | 831f8d6006 | Verdict branch changed from `!report.deviations.isEmpty()` to `identityDiffers` (identity dims only) |
| #1580 | experiment/reproduction_bundle_import | FIXED_OPEN | 831f8d6006 | `RunId::fromString` gate at :352 refuses invalid manifest run_id with warning; fresh-id path uses `RunId::generate()` |
| #1581 | experiment/reproduction_bundle_import | FIXED_OPEN | 831f8d6006 | Import loads provenance.json and appends explicit "cannot re-attach, run stays an island" warning (:493-501) |
| #1582 | experiment/benchmark_runner | FIXED_OPEN | 831f8d6006 | Per-reason skip counters + coverage object (truth_rows/prediction_rows/matched/unmatched) ride in raw_metrics and survive the store round-trip |
| #1583 | dataset/fold_audit | FIXED_OPEN | c8ba519901 | foldTotal derived from `manifest.assignments()` max fold+1 (:88-95), not `config().foldCount`; explicit note when zero |
| #1584 | dataset/leakage_audit | FIXED_OPEN | c8ba519901 | Overlap gate now requires `left` AND `right` windowWidth/Height > 0 (:608-609) — symmetric wrt bucket order |
| #1585 | dataset/leakage_audit | FIXED_OPEN (test gap remains) | c8ba519901 | Unconditional `!(sample.id < other.id)` filter removed; dedup fires only for symmetric pairs (:440-442) |
| #1586 | dataset/dataset_store_splits | FIXED_OPEN | c8ba519901 | saveLeakageReport existence SELECT + INSERT now wrapped in one BEGIN IMMEDIATE transaction with rollback paths |
| #1635 | experiment/evaluation | FIXED_OPEN | 831f8d6006 | nearMatch/nearTruth duplication consolidated into `buildCells` + `nearInCells` helpers; single `cellSize`, no `predictedCellSize` alias |

No issue is LIVE or PARTIAL. All ten defects described at baseline are corrected in production code at HEAD. Residual observations (non-blocking) are listed per issue below; the only substantive residual is missing discriminating regression tests for #1546, #1547, and #1585 — neither fix commit touched `tests/`.

---

## Per-issue evidence

### #1546 — repeat_execution variable shadowing — FIXED_OPEN

Fix: `git log -1 --format='%h %s' -S'unreadableByRef' -- src/experiment/repeat_execution.cpp` → `831f8d6006 fix(src/experiment): resolve full-review findings #1546 #1547 #1580 #1581 #1582 #1635`. The fix is a one-line deletion of the inner declaration (confirmed in the commit diff: `-            int unreadableByRef = 0;`).

Current code — one declaration, one increment, live guard:

- src/experiment/repeat_execution.cpp:201 `int unreadableByRef = 0;` (outer, inside `twins.isEmpty()` block)
- :215-227 the by-ref loop; :225 `++unreadableByRef;` increments the outer variable (only declaration in scope)
- :252-263 the refusal guard now reads the same variable:
  ```
  252  if ( !byRef.isEmpty() && unreadableByRef == byRef.size() )
  253  {
  256      return Result<Verdict>::failure( Diagnostic{
  257          QStringLiteral( "experiment.repeat_unreadable_twin" ), ...
  ```
- Grep confirms exactly 3 occurrences of `unreadableByRef` (:201, :225, :252) — no shadowing possible.

The increment at :224-225 is additionally narrowed to `experiment.run_corrupt` failures (`runRecordById`'s unreadable-row code, experiment_store.cpp:590), which is the correct "destroyed evidence" signal. Production path: `RepeatExecutionClassifier::classify` is public library API (no src/ caller yet; exercised by tests/test_experiment_evaluation.cpp, tests/test_experiment_consistency_r4.cpp).

Residual: the all-corrupt by-ref refusal branch (:252-263) has no test — tests/test_experiment_evaluation.cpp:1452 "repeat classifier refuses verdicts on unreadable identity twins" covers only the twin scan, same as at baseline (fix commit added no tests).

### #1547 — replay deviation verdict from non-identity dims — FIXED_OPEN

Fix: `git log -1 -S'identityDiffers' -- src/experiment/replay_deviation.cpp` → `831f8d6006`. Pre-fix branch was `else if ( !report.deviations.isEmpty() )` (confirmed in commit diff), which flipped Deviated on any appended diagnostic.

Current code:

- src/experiment/replay_deviation.cpp:125 `bool identityDiffers = false;` set only when `item.differs && kIdentityDimensions.contains( item.dimension )` (:128-133)
- :137-143 non-identity dims (artifacts/runtime) append `diagnostic '%1' differs: ...` entries that ride as record but cannot decide the verdict
- :159-167 verdict branch:
  ```
  159  else if ( identityDiffers )
  161  {
  161      // Identity-only gate, per the header contract: "deviated" means at
  166      report.verdict = ReplayDeviationReport::Verdict::Deviated;
  ```
  (The :184 Deviated on "result fingerprints differ under Strict determinism" is a separate pre-existing, documented rule — a Strict-claiming run failing to reproduce — not the artifacts/runtime flip the issue flagged.)

Header contract (replay_deviation.h:10-14) and implementation now agree. Production path: `ReplayDeviationAnalyzer::analyze` is public library API (tests only today).

Residual: no regression test for the exact scenario (same identity + wall-clock/artifact-digest drift → NOT Deviated); tests/test_mlops9_evidence.cpp:359-393 covers identical / deviated-by-identity-pin / incomplete only.

### #1580 — unvalidated manifest run_id at import boundary — FIXED_OPEN

Fix: `git log -1 -S'requestedRunId' -- src/experiment/reproduction_bundle_import.cpp` → `831f8d6006`.

Current code (src/experiment/reproduction_bundle_import.cpp:342-378):

```
352  const auto requestedRunId = RunId::fromString( report.originalRunId );
353  if ( !requestedRunId )
355  {
355      report.warnings.append(
356          QStringLiteral( "manifest run_id %1 is not a valid run id"
357                          " (filename-safe, at most 64 chars,"
358                          " [A-Za-z0-9._-], no leading dot) — import"
359                          " refused rather than sanitized" ) ...
361      return report;
363  run.setRunId( requestedRunId->toString() );
367  if ( m_experimentStore->runById( report.originalRunId ) )   // collision refusal
378  run.setRunId( RunId::generate().toString() );               // fresh-id path
```

`RunId::fromString` (experiment_ids.cpp:50-67) enforces exactly the advertised rules: non-empty, ≤64 chars, no leading dot, `[A-Za-z0-9._-]` — matching the issue's threat list (path separators, leading dot, >64 chars). The gate is no longer dead code: `RunId::fromString` now has exactly one production caller, here (grep: experiment_ids.cpp:50 def, reproduction_bundle_import.cpp:352 call). The downstream file-path sink cited in the issue (experiment_studio_dock.cpp:1071-1072 `capsule-%1.json`) is now protected because store ids are filename-safe by validation.

Production path: `ReproductionBundleImporter::importRun` — public library API (tests only today: tests/test_experiment_evaluation.cpp, tests/test_r21_b_end_to_end.cpp, tests/test_r15_B_geodata_persistence.cpp).

Residual (minor, cosmetic — present in the issue as supporting evidence, not the defect): run_recorder.cpp:36-39 `newRunId()` still returns a hyphenated UUID (`QUuid::...WithoutBraces`, 36 chars) while `RunId::generate()` strips hyphens (32 hex, experiment_ids.cpp:42-47). Both shapes pass `RunId::fromString`, so this is an id-shape inconsistency, not a gate bypass.

### #1581 — provenance.json silently dropped on import — FIXED_OPEN (gap now named)

Fix: `git log -1 -S'nearInCells'`-style marker for this file: commit 831f8d6006 (see `-S'requestedRunId'` above; same commit, +73 lines in this file).

Current code (src/experiment/reproduction_bundle_import.cpp:466-501) — step 7 loads the slice and names the gap:

```
478  const auto provenance =
479      loadJson( dir, QStringLiteral( "provenance.json" ), &sliceWarnings );
...
493  report.warnings.append(
494      QStringLiteral( "provenance.json records %1 lineage edge(s) around"
495                      " the recorded run that import cannot re-attach"
496                      " (the bundle keeps only each edge's far endpoint,"
497                      " so the run-relative direction is lost); the"
498                      " imported run stays an island in the lineage"
499                      " graph until it is re-linked by hand" )
500          .arg( edges.size() ) );
```

The "cannot re-attach" justification is accurate against the exporter: reproduction_bundle.cpp:419-428 writes each edge as `{"edge","other_kind","other_id"}` where the ternary `edge.from.id == run.runId() ? edge.to.* : edge.from.*` collapses the run-relative direction, and the importer constructor takes only `ExperimentStore` (reproduction_bundle_import.h:60) — no DatasetStore for dataset-side edges. Re-attaching would guess direction or fabricate provenance. The issue offered two acceptable remedies ("既不 addLineageEdge 重建这些边，也不在 warnings 里说明" was the double defect); the fix implements the warning leg consistent with the repo's "named, never silent" doctrine, and malformed/missing slices also warn (:484-492).

Residual: imported runs remain lineage islands by documented design; a format change (export `from`/`to` roles) would be needed to ever re-attach. No test asserts the new warning text (fix commit touched no tests).

### #1582 — benchmark classification silently skips unmatched rows — FIXED_OPEN

Fix: `git log -1 -S'predictionsWithoutTruth' -- src/experiment/benchmark_runner.cpp` → `831f8d6006`.

Current code:

- src/experiment/benchmark_runner.cpp:370-409 — every skip reason counted by name: `predictionsWithoutTruth` (:385), `predictionsWithUnknownTruthLabel` (:392), `predictionsWithUnknownPredictedLabel` (:397), `predictionsIgnoredByLabel` (:403); `matched`/`matchedIds` (:407-408)
- :413-418 `truthRowsWithoutMatrixCell` — truth rows that contributed no matrix cell
- :446-463 coverage object rides in `raw_metrics`:
  ```
  448  coverage.insert( QStringLiteral( "truth_rows" ), qint64( request.truths.size() ) );
  449  coverage.insert( QStringLiteral( "prediction_rows" ),
  450                     qint64( request.predictions.size() ) );
  451  coverage.insert( QStringLiteral( "matched_samples" ), matched );
  452  coverage.insert( QStringLiteral( "unmatched_prediction_rows" ), ...
  ```
- Round-trip verified: `BenchmarkResult::toJson` writes `raw_metrics` (:61-62) and `fromJson` restores it verbatim (:115), so the counts survive the store.

A 5/10000 match still stores Completed with full metrics (by design — metrics are honest over the matched subset), but the coverage record now distinguishes it from a full evaluation, which is what the issue demanded ("name the gap" doctrine).

Production paths (live): `BenchmarkRunner::run` ← src/experiment/benchmark_service.cpp:150 and src/experiment_studio/live/studio_live.cpp:348.

### #1583 — auditFolds iterates config.foldCount instead of assignment folds — FIXED_OPEN

Fix: `git log -1 -S'foldTotal' -- src/dataset/fold_audit.cpp` → `c8ba519901 fix(src/dataset): resolve full-review findings #1583 #1584 #1585 #1586`.

Current code (src/dataset/fold_audit.cpp:88-101):

```
 88  int foldTotal = 0;
 89  for ( const SplitAssignment &assignment : manifest.assignments() )
 90  {
 91      if ( assignment.fold < 0 )
 92          continue;
 93      if ( assignment.fold + 1 > foldTotal )
 94          foldTotal = assignment.fold + 1;
 95  }
 96  summary.foldCount = foldTotal;
 97  if ( foldTotal <= 0 )
 98  {
 99      summary.notes.append( QStringLiteral(
100          "manifest carries no fold assignments; no fold could be audited" ) );
101  }
```

Both defects are closed: (a) the fold base now comes from the assignments, so leave-one-{region,scene,year}-out manifests (split.cpp:1236-1247, one fold per distinct key, never reading foldCount) are audited over their real fold set; (b) `config().foldCount` is no longer read anywhere in the file (grep count 0), eliminating the unbounded-qint64 → `int()` narrowing that could yield a zero-iteration "clean" audit; the zero case now produces an explicit note (:97-101). Comment at :79-87 documents the rationale. Production path: `FoldAuditor::auditFolds` — public library API (tests/test_leakage_audit_coverage.cpp:352, tests/test_platform7_library.cpp:609+ today).

### #1584 — overlapping_patch gate checks only left side — FIXED_OPEN

Fix: `git log -1 -S'right.windowWidth' -- src/dataset/leakage_audit.cpp` → `c8ba519901`. Commit diff shows the one-line gate widened from `left.windowWidth > 0.0 && left.windowHeight > 0.0` to both sides.

Current code (src/dataset/leakage_audit.cpp:603-609):

```
603  // Patch overlap needs window evidence on BOTH sides:
604  // left/right follow bucket enumeration order (self cell
605  // first, then right/lower neighbors), so gating on one
606  // side alone makes the same pair's report depend on which
607  // sample happened to land in the earlier bucket.
608  if ( overlapCheck && left.windowWidth > 0.0 && left.windowHeight > 0.0 &&
609       right.windowWidth > 0.0 && right.windowHeight > 0.0 )
```

The predicate is now symmetric with respect to which sample lands in the earlier bucket (the spatial artifact the issue described), so whether a pair is evaluated no longer depends on bucket enumeration order (:559-571 self-cell then right/lower neighbors). Requiring both sides also prevents a degenerate zero-area denominator in the fraction at :630. Production paths (live): `LeakageAuditor::audit` ← src/agent/data_platform_tools.cpp:830 and fold_audit.cpp:142.

### #1585 — one-sided pre/post pair dedup drops reverse-direction links — FIXED_OPEN (test gap remains)

Fix: `git log -1 -S'other.pairCounterpartId == sample.input.sampleId' -- src/dataset/leakage_audit.cpp` → introduced by `a27f8601b0`, but the #1585 defect lived in the OLDER unconditional filter that baseline `2d2881631f` still had on top of it:

```
if ( !( sample.input.sampleId < samples.at( *otherIt ).input.sampleId ) ) continue;   // baseline :432 — the bug + made the next condition dead
```

`c8ba519901` deleted exactly that line (confirmed in the commit diff), leaving only the symmetric-pair dedup (current :440-442):

```
440  if ( other.pairCounterpartId == sample.input.sampleId &&
441       !( sample.input.sampleId < other.input.sampleId ) )
442      continue;
```

Behavior verified by trace: symmetric pair emits once from the lower-id side (higher-id side skips); one-sided reference never satisfies `other.pairCounterpartId == sample.input.sampleId`, so it reports from its linking side regardless of id order — the previously-dropped reverse case (linking side has the LARGER id) now reports. The dead second continue is gone (merged into the single compound condition). Production path (live): `LeakageAuditor::audit` ← src/agent/data_platform_tools.cpp:830.

Residual: the discriminating reverse-direction test is still absent. tests/test_leakage_audit_coverage.cpp:232-254 ("one-sided counterpart references report exactly once") uses linking side `"pair-hi"` which is lexically LOWER than `"pair-lo"` — that case also passes under the pre-fix code, so the regression is unguarded. A test with e.g. linking id "zz-link" → counterpart "aa-silent" would lock the fix in.

### #1586 — saveLeakageReport check-then-insert without transaction — FIXED_OPEN

Fix: `git log -1 -S'cannot begin transaction' -- src/dataset/dataset_store_splits.cpp` → `c8ba519901`.

Current code (src/dataset/dataset_store_splits.cpp:232-316): the existence SELECT (:258-274, `SELECT 1 FROM split_manifests WHERE manifest_id=?`) and the INSERT (:288-308, `INSERT OR IGNORE INTO leakage_reports...`) now ride one transaction:

```
247  // Existence check + append ride ONE transaction (BEGIN IMMEDIATE, same
248  // shape as saveQaReport below) so a report can never attach to a split
249  // manifest deleted between the check and the insert: ...
254  if ( !m_impl->begin( nullptr ) ) ...
```

`m_impl->begin` is `exec("BEGIN IMMEDIATE")` (dataset_store_impl.h:134-136) — same shape as saveSplitManifest (:44+) and saveQaReport. Every failure path rolls back (:262, :269, :294, :304, :311); commit at :309. The orphan-row race the issue described (cross-connection deleteDataset cascade vs insert into a no-FK table) is closed. Production path (live): `DatasetStore::saveLeakageReport` ← src/agent/data_platform_tools.cpp:838.

### #1635 — evaluation.cpp boundaryFScore duplicated match lambdas — FIXED_OPEN

Fix: `git log -1 -S'nearInCells' -- src/experiment/evaluation.cpp` → `831f8d6006`.

Current code (src/experiment/evaluation.cpp:393-449): the ~35-line nearMatch/nearTruth duplication is consolidated into two parameterized helpers:

```
394  const qint64 cellSize = qMax<qint64>( 1, radius );        // the ONE size
396  auto buildCells = [&]( const QVector<qint64> &boundary ) { ... }   // shared bucketing
408  auto nearInCells = [&]( const QHash<...> &cells, qint64 index ) { ... }  // shared 3×3 + dist2 <= tolerance2 scan
433  const QHash<...> predictedCells = buildCells( predictedBoundary );   // recall side
443  const QHash<...> truthCells = buildCells( truthBoundary );           // precision side
```

The `predictedCellSize` Mysterious Name is gone — grep shows a single `cellSize` (:394) used by both `buildCells` (:402) and `nearInCells` (:411-412), so the "change one constant only" hidden-mismatch hazard is structurally eliminated. Both sub-items of the tech-debt entry are resolved. Production path: `boundaryFScore` declared evaluation.h:134 — public library API (tests/test_experiment_evaluation.cpp:220-238 today).

---

## Cross-cutting observations

1. Neither fix commit added tests (`git show --stat`: 831f8d6006 = 5 src files; c8ba519901 = 3 src files, 0 test files). The three discriminating regression tests that would lock in #1546 (all-corrupt by-ref scan), #1547 (same-identity + artifacts/runtime drift ≠ Deviated) and #1585 (one-sided link from the higher-id side) are still absent — the closest existing tests pass under the pre-fix code as well.
2. R21-B follow-up `388f7db02f` (+19 lines in reproduction_bundle_import.cpp) added the A5 orphan-metric compensation (`deleteMetricRecord` undo on failed run install) and did not touch the run_id validation or provenance warning — no regression.
3. Live production exposure today: leakage audit + leakage report persistence (src/agent/data_platform_tools.cpp:830/838) and benchmark runs (benchmark_service.cpp:150, experiment_studio/live/studio_live.cpp:348). FoldAuditor, boundaryFScore, RepeatExecutionClassifier, ReplayDeviationAnalyzer and ReproductionBundleImporter currently have no src/ production callers — they are public library API exercised by tests, so even the pre-fix defects were library-surface, and all are fixed regardless.
