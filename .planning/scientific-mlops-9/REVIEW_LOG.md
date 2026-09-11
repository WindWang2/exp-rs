# REVIEW LOG — scientific-mlops-9

## Round 0 — self-review (main agent, full diff vs master)

Findings fixed during development (each verified by the named test):

1. **Fingerprint drift (P1, caught by the test_split_leakage regression)**:
   the generation summary was appended to the manifest AFTER the
   fingerprint was stamped, so a reloaded manifest hashed differently than
   at generation time. Fix: `splitManifestFingerprint` excludes the summary
   as derived content — stored pre-9.0 fingerprints stay valid.
2. **CLI terminal recording could miss the Failed transition (P1, caught by
   test_mlops9_cli_record)**: a headless run has no long-lived event loop;
   the queued runStateChanged delivery was still pending at return, so the
   Failed terminal never landed. Fix: bounded drain + recording the
   terminal outcome directly from the coordinator's run aggregate
   (`recordAggregateState`, content-identical to the queued event), then
   flush() drains duplicates; the bridge is idempotent for repeats.
3. **Scale suite constructed invalid terminal runs (P2)**: terminal status
   without finish time is refused by the store's read contract — the suite
   now stamps truthful times; the ref-scan needle is unambiguous.
4. **Shadowed local `criteria` (P2, compile)** in PromotionRequest::toJson;
   QSet::insert().second misuse (Qt6 API).
5. **Result→optional conversions (P2, compile)** in the new store methods.

## Rebase event (between rounds 0 and 1)

origin/master moved during development: f316dfdbb4 "resolve all 30 P1/P2
issues (#853-#882)" landed (the parallel execution-track remediation
wave). The branch was rebased onto it. One conflict (split.cpp
validate()); the first resolution accidentally took the base version — a
rebase ours/theirs inversion — which the verification battery caught
immediately (9 split-suite failures) and which was fixed by autosquashing
the correct superset file back into the M0 commit. Post-rebase, all 10
suites (1762 assertions) re-ran green on the merged tree.

The new master's own #875 fix is the minimal
`(SpatialBlock || SpatialKFold) && blockSize <= 0` guard; it does not
cover NaN (comparison false), negative ratios, grid-index overflow, or
degenerate folds. This branch's M0 remains the superset; no double-fix.

## Round 1 — read-only subagent A (architecture / correctness / concurrency / scientific validity)

Counts: P0=0, P1=1, P2=5, P3=8. Verdict: "architecturally sound and
authority-boundary-clean; mergeable modulo fixes".

| # | Sev | Finding | Disposition |
|---|---|---|---|
| A-1 | P1 | M6 artifacts/runtime dimensions flip M7 replay verdicts (wall-clock jitter → "deviated") | **FIXED**: verdict gated on the seven identity dimensions only; artifacts/runtime surface as labelled diagnostics; Strict-determinism fingerprint mismatch (either side) now yields Deviated per the header contract |
| A-2 | P2 | Matrix cellId global across matrices → cross-matrix ledger contamination | **FIXED**: matrix_id is part of the cell identity hash |
| A-3 | P2 | RunPins seed as JSON double loses precision >2^53 | **FIXED**: seed_hex string form written; legacy numeric form still read |
| A-4 | P2 | PromotionRecord equality formatting-sensitive (Compact vs Indented) | **FIXED**: fromJson canonicalizes criteriaJson to Compact; positive idempotent-resave test added |
| A-5 | P2 | flushRecording discards the recording failure (silent stranded Running) | **FIXED**: member method reporting refusals through reportLog |
| A-6 | P2 | versionAncestors default depth 64 contradicts the write bound 256 | **FIXED**: default is kMaxVersionLineageDepth |
| A-7 | P3 | donor-selection comment says "LAST group" but ties resolve earliest | FIXED: comment corrected |
| A-8 | P3 | header claims all doubles finite-checked; only consuming methods check | FIXED: SplitConfig header doc scoped precisely |
| A-9 | P3 | M2 validity windows are storage+validation only | FIXED: header doc states the scope explicitly |
| A-10 | P3 | matrix "missing" also swallows in-flight runs | FIXED: new "in_progress" status + count asymmetry documented |
| A-11 | P3 | runsForCell silently truncates at 100 | ACCEPTED: cells are single-submission units by contract; cap matches store paging conventions — noted in the header |
| A-12 | P3 | record() persists a caller-supplied evaluation | FIXED: record() re-derives the evaluation from the store |
| A-13 | P3 | Strict fingerprint mismatch inspects only original determinism | FIXED (with A-1): either side Strict ⇒ Deviated |
| A-14 | P3 | derived manifest inherits parent createdAtUtc | FIXED: fresh stamp |

## Round 1 — read-only subagent B (tests / performance / portability / resources / docs-vs-code)

Counts: P0=0, P1=1, P2=6, P3=7. Verdict: "tests genuinely verify the
milestone claims... RUN_SERIAL claim must be fixed before merge".

| # | Sev | Finding | Disposition |
|---|---|---|---|
| B-1 | P1 | RUN_SERIAL target property is a no-op for discovered tests | **FIXED**: RUN_SERIAL TRUE passed through sicnu_add_test → discovery properties; the no-op property lines removed |
| B-2 | P2 | tautological CHECK (executionRef == itself) | FIXED: real non-empty assertion |
| B-3 | P2 | versionAncestors depth 64 vs documented 256 (+off-by-one) | FIXED (with A-6): shared constant; docs aligned |
| B-4 | P2 | non-canonical criteriaJson makes the conflict rule dead / test vacuous | FIXED (with A-4): canonical Compact + positive idempotent-resave case |
| B-5 | P2 | MatrixAggregate::toJson declared, never defined | FIXED: implemented |
| B-6 | P2 | "linear" ref-scan claim wrong (per-page ORDER BY re-sort → superlinear) | FIXED: PERFORMANCE.md and the test comment state the measured behaviour and the covering-index follow-up |
| B-7 | P2 | replay "Identical" verdict wall-clock dependent | FIXED (with A-1): runtime is no longer verdict-relevant |
| B-8 | P3 | scale wall-clock headroom | MITIGATED: RUN_SERIAL now real; bounds re-stated |
| B-9 | P3 | CWD-relative sqlite files in version_dag test | FIXED: QTemporaryDir everywhere |
| B-10 | P3 | manual arm/disarm instead of RAII | FIXED: ArmedFault RAII, inner scope so disarm precedes the recovery write |
| B-11 | P3 | missing <sys/wait.h> | FIXED |
| B-12 | P3 | planning-doc drift (phantom diagnostic code, PASS-vs-queued contradictions, unresolvable pointers, missing CHANGELOG) | FIXED: ARCHITECTURE rewritten to the actual diff; TEST_MATRIX/MILESTONES consistent; CHANGELOG entry added |
| B-13 | P3 | "documented O(n·k)" SpatialBuffer pointer unresolvable | FIXED: reworded as a code-inherent property |
| B-14 | P3 | corruption test only covers whole-file garbage | ACCEPTED: page-level lazy corruption needs SQLite-internal fault injection — out of 9.0 scope; recorded as follow-up |
| B-15 | P3 | no-fabrication CLI case passed vacuously | FIXED: recording enables BEFORE the resume attempt; the test asserts experiment-present + runs-empty unconditionally |

## Dispositions summary

Round 1: all P0/P1 fixed, all P2 fixed, P3 fixed except two
accepted-with-rationale items (B-14 corruption depth, A-11 ledger cap).
The full battery re-ran green after remediation (final numbers in
FINAL_REPORT.md).
