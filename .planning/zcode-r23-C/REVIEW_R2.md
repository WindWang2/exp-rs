# REVIEW R2 — independent adversarial re-derivation (r23-C worktree)

Reviewer: REVIEWER 2, no prior context. Every claim below was re-derived from
`git diff origin/master...HEAD` and the current sources, then verified by
running the built test binaries (5x repeat) and a Qt 6.8 `%n` probe.
`REVIEW_R1.md` was NOT read until the cross-check section at the end.

## Verdict

**FIX-THEN-SHIP** — one P2 (documentation overclaim on WP-2's core
guarantee; fix is a sentence, not code). No code-behavior defect found in
WP-1/WP-2/WP-3. All P3s are optional polish.

Evidence baseline: `tests/test_r23_c_task_center_log_spaces` (24 assertions)
and `test_r23_c_teaching_pair_publish` (25 assertions) both pass; the
task-center suite was run 5 consecutive times, all green.

---

## Findings

### R2-F1 (P2) — "The two artifacts can never disagree" is overclaimed; two corners can disagree

- `src/teaching_admin/batch_assessment.h:186-192` and the catch comment at
  `src/teaching_admin/batch_assessment.cpp:1002-1010` state the pair
  "can never disagree". Enumerating every `return false` path (mandate §4):

  | path | on-disk result | agrees? |
  |---|---|---|
  | directory pre-flight (`batch_assessment.cpp:981-985`) | nothing staged, old pair | yes |
  | staging failure (`:989-993`) | staged files discarded, old pair | yes |
  | `writeJournalFile` throws (`atomic_fs.cpp:394/404/407` via ENOSPC/open) | thrown before any target mutation, old pair | yes |
  | member publish failure → `cleanup()` (`atomic_fs.cpp:885-936`) | targets rolled back to old pair | yes — **except** R2-F1a |
  | commit-marker failure (`atomic_fs.cpp:962-971`) | NEW pair + journal/backups kept; recoverable to old; retry idempotent | yes (both-new) |
  | success (`:974-977`) | new pair, backups/journal swept | yes |

  **R2-F1a (restore-failure corner).** If the rollback itself fails
  (`restoreFailures` non-empty, `atomic_fs.cpp:924-933` — e.g. a locked
  target on Windows, or an unwritable directory), one member can be
  restored-old while the failed member stays-new: a mixed pair, and
  `publishBatchOutputsAtomic` still returns false. Worse, `cleanup()`
  deletes the journal unconditionally (`atomic_fs.cpp:920`) *before*
  throwing with `restore_failures`, so `recoverDatasetGroup()` can no longer
  see the incident; the retained `.bak` of the unrestored member is then
  swept by the next publish's start-of-run pass (`atomic_fs.cpp:860-870`),
  cementing the mixed pair. Reachability needs two simultaneous I/O
  failures, so this is rare — but "never" is false.

  **R2-F1b (concurrent same-prefix publish).** Two processes (two teachers,
  shared/network volume) publishing the same prefix can interleave member
  renames (P1.json, P2.json, P2.csv, P1.csv → P2 json + P1 csv), and each
  run's start-of-run `recoverDatasetGroup` (`atomic_fs.cpp:853`) can roll
  back the *other* run's in-flight publish. No inter-process lock exists in
  the primitive. In-app this is serialized
  (`src/app/teaching_admin/teaching_admin_dock.cpp:377` early-returns while
  `m_batchThread.joinable()`), so exposure is cross-process only — a lab
  shared drive is the realistic setting for this product.

  **Suggested fix (docs-only, in this branch's files):** soften the header
  sentence to "…never disagrees under single-writer publication; an
  unrestorable rollback (locked targets) or two concurrent publishes of the
  same prefix are the documented exceptions" — or add single-writer to the
  contract the way the pre-flight directory refusal is documented. Keeping
  the journal on restore-failure (`atomic_fs.cpp:920` gated on
  `restoreFailures.empty()`) would be the code fix, but `atomic_fs.cpp` is
  pre-existing code outside this diff — follow-up, not this branch.

### R2-F2 (P3) — `publishStagedMembers` silent-skip can fake success

`atomic_fs.cpp:842-850`: staged members that are missing at publish time are
filtered out, and an empty filtered list is *success*. If anything deleted a
staged `.tmp` file between `writeBytesAtomically` and publish,
`publishBatchOutputsAtomic` returns true with one/both artifacts never
landed. Practically unreachable for this caller (fresh unique stage names,
milliseconds apart), and it is documented primitive behavior — note only.

### R2-F3 (P3) — stale member doc comment now contradicts the new invariant

`src/processing/framework/task_center.h:646` still documents
`m_forwardedLogCounts` as `(logLines.size())`. After this branch it stores
the ABSOLUTE appended-line count (`droppedLogLines + size()` cumulative, or
`logLinesOffset - 1 + k` delta — `task_center.cpp:2042/2062`). The comment
describes exactly the retained-window confusion the fix removes. One-line
doc fix.

### R2-F4 (P3) — cumulative branch silently accepts the eviction loss window

`task_center.cpp:2055-2063`: when `forwarded < droppedLogLines`, lines
`[forwarded, dropped)` are skipped forever and the counter jumps to
`absTotal`. That is correct (the engine already evicted them; the delta lane
clamps identically at `job_engine.cpp:991-995`) but the block comment
documents only the `dropped == 0` degeneration, not the accepted-loss case.
One sentence would prevent a future "fix" that tries to re-request the
dropped range.

### R2-F5 (P3) — new plural string is not in the zh_CN catalog; "entry(ies)" is translator-hostile

`resources/translations/sicnu_zh_CN.ts` contains neither the old
`entr%2` string nor the new `%n entry(ies)` one (the branch does not
regenerate the .ts). This matches repo practice — lupdate runs via the
explicit `sicnu_i18n_update` target (`CMakeLists.txt:996-1002`) and
`scripts/check_translation_freshness.py` is referenced by no CI workflow or
pre-commit hook, so nothing fails. Note for the next i18n pass: the
translators must rewrite both plural forms of "entry(ies)" anyway, so the
parenthesized pseudo-plural buys nothing over "entry(s)" or a clean rewrite.

### R2-F6 (P3) — relative `outPrefix` keeps journal/backup paths relative

With a directory-less prefix (relative filename), staging resolves through
`weakly_canonical(".")` (`atomic_fs.cpp:117-123`) but target, `.bak`, and
journal paths stay relative (`atomic_fs.cpp:874-875`) — self-consistent
within one process/CWD, mismatched if recovery ever runs from another CWD.
App callers pass absolute prefixes (dock file dialog); pre-existing
primitive property, not introduced here.

---

## Per-mandate notes

### 1. WP-1 adversarial math — all interleavings exactly-once (confirmed)

Engine record shapes re-derived from source (not from any review):

- Running/`startedCopy`: full copy, `logLinesOffset == 0`, `droppedLogLines`
  copied (`job_engine.cpp:857-868`) — cumulative.
- log-callback delta: `logLinesOffset = dropped + localStart + 1` —
  ABSOLUTE index base (`job_engine.cpp:996`), matching the consumer's
  `first = logLinesOffset - 1` (`task_center.cpp:2032`). The two branches
  now share one index space. Verified consistent.
- progress callback: full cumulative copy (`job_engine.cpp:1024`).
- terminal: delta when a cursor exists, else full cumulative
  (`job_engine.cpp:1104-1123`); the fields-only variant clears `logLines`
  but keeps `droppedLogLines` → cumulative with empty window: new code
  computes `absTotal = dropped`, updates the counter only if `dropped >
  forwarded` (accepted-loss window, see R2-F4). No behavioral regression.
- submit Queued notify (`job_engine.cpp:312`) and cancelled-while-queued /
  cancelled-due-to-shutdown full records (`:341`, `:154`): cumulative, and
  after 10k-line eviction the OLD guard (`size() > forwarded`) would lose
  their line — the R2-F6 shape the fix repairs. Confirmed the fix delivers
  it: `absTotal > forwarded` with `start = max(forwarded, dropped) -
  dropped` slices exactly the unforwarded retained tail.

Interleavings hand-simulated against `task_center.cpp:2018-2064` (single
`m_mutex` section; counter read+update atomic under it):

- catch-up snapshot (submitting thread, `task_center.cpp:3119-3120`) vs
  listener deltas in both orders, with and without the snapshot spanning an
  eviction boundary: every line forwarded exactly once; delta skip guard
  (`engineIndex < seen`) and cumulative slice agree in all orders.
- stale progress-cumulative after newer deltas (seen=90, window [30,100)):
  forwards exactly [90,100), counter → 100. Old code lost [90,100) (guard
  `70 > 90` false) — the R2-F3 shape; fixed.
- cumulative with empty `logLines` / `droppedLogLines == 0` legacy records:
  degenerates to the historical no-op/slice. Verified.
- forwarded > absTotal after a foreign/queued record: guard false, counter
  not clobbered (a stale record can never lower or raise the counter
  wrongly). Verified.
- `retryTask` resets dedup (`task_center.cpp:3651`) and the retry job is a
  NEW engine job whose absolute space restarts at 0 ("Started" is index 0,
  `job_engine.cpp:857` — submit never appendLogs, verified against every
  appendLog site) — consistent. Terminal cleanup removes the counter
  (`:2099`) and the `isTerminalStatus` guard (`:1949`) rejects late
  duplicates, so a late catch-up snapshot after Succeeded cannot re-append.

### 2. Test seam — safe, same locking as production

`deliverJobRecordForTests` (`task_center.cpp:553-556`) is a pass-through to
`onJobRecord`; production records arrive via the listener lambda
(`task_center.cpp:1895-1896`) — the exact same entry, the exact same
section-wise `m_mutex` locking, callable from any thread. It mutates no
state at rest and has no callers outside the new test (grepped). It is
compiled into production like the pre-existing `shutdownForTests`
precedent. The one theoretical difference — concurrent entry from two
threads — is an *arity* the engine itself already has (multi-worker notify),
and the tests keep the engine parked/quiet during replay (the last chatty
delta is notified synchronously inside `ctx.logInfo` before the worker
parks, so `countLine("abs 11999")==1` implies no further engine records are
in flight). Safe.

### 3. New test file — flakiness audit: sound

- **"Started" anchor:** appended exactly once per job under the engine lock
  (`job_engine.cpp:857`) before the startedCopy notify; no submit-path
  appendLog exists. The status flip precedes the log append inside
  `processJobRecord` (`task_center.cpp:1978-1996` vs `:2018+`), so waiting
  for Running alone is genuinely insufficient — the anchor loop is
  necessary and `REQUIRE(countLine(...)==1)` makes a timeout loud.
- **base >= 2 guaranteed:** the queued line is appended unconditionally at
  staging (`task_center.cpp:1725`), independent of admission (the parked
  executor registers no descriptor → no resource gate). base = queued +
  Started = 2; all subsequent size assertions are base-relative and hold
  for any base ≥ 2 (checked algebraically: logBuffer size and the synthetic
  absolute space stay equal because the +1 local queued line cancels the
  Started-line offset — structurally, for every base).
- **countLine semantics:** exact `QString ==` over whole logBuffer entries
  (`std::count`), so "abs 1199" cannot match "abs 11999", and no other line
  equals "Started" (queued line format is `[ts] Task queued with priority
  N.`).
- **Ring bounds:** per-task ring trims at >4000 down to newest 2000 + elision
  marker (`task_center.cpp:98-108`). Case A peak ≈ 12 002 lines → trimmed to
  2001; both asserted needles are in the newest 2000. Case B peak = base +
  1202 ≈ 1205 < 4000 — whole-line counting exact, as the comment claims.
- **Constants:** engine cap 10 000 / keep 5 000 (`job_engine.h:284-285`):
  12 001 appends → dropped = 5001, retained = 7000, snapshot `absTotal` =
  12 001; the replayed record has 7001 lines — the test's "7001" comment is
  accurate. `droppedLogLines > 0` and `logLinesOffset == 0` both hold for
  `snapshot()` (plain copy, `job_engine.cpp:407-414`).
- **Singletons:** per-case `SingletonReset` ctor/dtor ordering is correct
  for sequential Catch2 cases; `ensureJobListener` re-claims the listener on
  every submit (`task_center.cpp:1891-1897`) so a reset cannot strand
  bookkeeping; `ParkedRelease`'s dtor fires the release on assertion abort
  and swallows the double-fire — the worker cannot strand.
- **Terminal asserts (R1-fix):** `waitUntilIdleForTests(15000)`'s ignored
  return is backstopped by the post-poll `REQUIRE(status == Completed)` in
  both cases.
- Ran the binary 5x: green each time.

### 4. WP-2 adversarial — see R2-F1 table for the full `return false`
enumeration

Additional attacks checked:

- **relative outPrefix:** stages land in `weakly_canonical(".")`, i.e. CWD;
  publish/journal paths stay relative but resolve to the same CWD within
  the process — consistent (R2-F6).
- **`.csv` target is a symlink:** to a directory → `QFileInfo::isDir()`
  follows links → refused pre-flight. To a file → `fileExists` follows, the
  `::link` backup and the publish rename both operate on the link itself;
  the target becomes a regular file with new bytes, backup keeps the old
  link. No mixed pair.
- **stale `prefix.csv.bak`:** swept or resurrected before the journal is
  written (`atomic_fs.cpp:860-870`) — a stale .bak cannot poison this run's
  rollback sources.
- **directory pre-flight TOCTOU:** a directory swapped in after the check
  collapses into a failed `rename` → member cleanup → old pair; the race
  costs an error return, not consistency. Accepted implicitly; the header
  could say so (R2-F1 wording pass covers it).
- **"CSV identical / JSON differs" staged reuse:** impossible — fresh
  `pid.counter.rand` stage names per call; nothing is reused.
- **fsync contract:** `writeBytesAtomically` routes through
  `platform::sidecar::write`, which fsyncs the staged content before its
  rename (`durable_sidecar.cpp:454`), so the staged files are durable
  before `publishStagedMembers` renames them — the atomic_fs
  "temp → write → flush" contract holds.
- **discardStaged sidecar sweep on stage names:** the `.tmp.json` leaf
  matches no sidecar suffix family — no collateral deletions.

### 5. CMake/ABI — clean

- `sicnu_geospatial` is the real target (alias `Sicnu::Geospatial`,
  `src/geospatial/CMakeLists.txt:70-71`); PRIVATE on a STATIC library
  propagates as `$<LINK_ONLY:...>` so the test/executable links resolve.
- No link cycle: geospatial links only GDAL/CURL/ws2_32; teaching_admin is
  a leaf. The forbidden-link guard (`Qt6::Widgets`, `qgis_gui`,
  `Qt6::Network`, `Sicnu::app`) is not violated by `sicnu_geospatial`.
- `AUTOMOC OFF`, `CXX_STANDARD 20`, `PIC ON` on teaching_admin are
  unaffected; the added headers need no moc (`#if __has_include(<QString>)`
  only).
- Both registrations use existing helpers verbatim
  (`tests/CMakeLists.txt:102`, `:14578`); both targets compiled and ran in
  `build-r23-C` (fresh `.o`s + green runs above) — from-scratch registration
  is proven.

### 6. i18n — correct

- Qt 6.8 (`CMakeLists.txt:95`). The rule "%n cannot coexist with %1" does
  not exist in Qt 5/6; `%n` is substituted by `translate()` and the
  remaining `%1` is filled by the chained `.arg(names...)` — order-safe.
- Empirical probe (Qt 6.8, `/usr/sbin` toolchain, no translator installed):
  `tr("... %n entry(ies) ... %1 ...", disamb, n).arg(x)` renders
  "discovery refused 1 entry(ies) ... name-a" / "refused 3 entry(ies)" —
  `%n` is substituted even untranslated; no literal `%n` leaks.
- lupdate/lrelease: the update target scans `src/app` (the dock is there);
  plural sources are standard lupdate input; no pipeline assertion exists
  (the freshness script is not wired into CI — see R2-F5 for the catalog
  state).

### 7. Hygiene / revert incident — final tree correct

- `f878807475` ("use the public logInfo surface") accidentally deleted the
  WP-2 CMake edge and gutted `batch_assessment.cpp` (73 lines removed);
  `41aa25c7f3` restored it. Verified: `git diff 57df09e502 HEAD` on the
  batch_assessment pair shows ONLY the R1-review comment/header changes —
  the restored logic is byte-identical to the original fix, present exactly
  once, and the old write-final-path sequence is fully gone (single
  definition of `publishBatchOutputsAtomic`; no `writeBytesAtomically(
  jsonPath/csvPath` remnants).
- `git diff fb1b2e8489 HEAD -- src/processing/framework/task_center.cpp`
  is empty — the WP-1 fix is intact through the R1-fix commit.
- Includes: no unused additions in the new test (algorithm/future/chrono/
  thread/string all used); `geospatial/common.h` in batch_assessment.cpp is
  redundant (atomic_fs.h includes it) but explicit — fine. No shadowing
  introduced. The `batchCsvBytes` extraction is a verbatim move of the CSV
  builder (BOM-bytes comment and sort preserved).

### R1-fix commit audit (c875659c00)

- **Anchor change (R1-F1):** correct and necessary (status flip precedes
  log append — see §3). The new `REQUIRE` makes the wait loud.
- **`setMaxWorkers(1)` removal (R1-F4):** confirmed the claim —
  `setMaxWorkers` clamps to `kMinWorkers = 2` (`job_engine.cpp:190`,
  `job_engine.h:74`), so the call was a no-op lie.
- **Terminal asserts (R1-F5):** added in both cases; closes the silent
  6-second-timeout hole.
- **Comment/header honesty (R1-F2/F3/F7):** verified against
  `atomic_fs.cpp` — member failure does roll back; the commit-marker corner
  keeps the new pair + journal (and `recoverDatasetGroup` on the
  uncommitted journal restores the OLD pair, so either resolution stays
  self-consistent; a retry re-publishes idempotently). The fixed wording is
  accurate for every path EXCEPT the two R2-F1 corners it still papers over
  with "never disagrees".
- **No new problems introduced** by the fix commit: base>=2 is guaranteed,
  the anchor cannot over-match, and the added REQUIREs can only fail more
  honestly.

---

## R1 cross-check (written after the analysis above)

Read `REVIEW_R1.md` only now. R1's findings and the fixes in c875659c00:

- **R1-F1 (P2, anchor race):** holds — I independently identified the same
  flip-before-append ordering (status flip at `task_center.cpp:1988` vs
  append at `:2067`, two lock sections apart) and R1's off-by-one
  arithmetic checks out; the Started-line fix is sound and necessary (my
  §3). No regression.
- **R1-F2/F7 (dishonest catch comment / commit-marker corner):** held at
  the time; the rewritten comment is accurate for the two paths it names
  but still overclaims "never disagrees" (my R2-F1a/R2-F1b) — the same
  class of issue, one notch deeper.
- **R1-F3 (header contract):** now honest except the same final sentence
  ("The two artifacts can never disagree" — see R2-F1).
- **R1-F4 (`setMaxWorkers` no-op):** confirmed independently
  (`kMinWorkers = 2` at `job_engine.h:74`, clamp at `job_engine.cpp:190`).
- **R1-F5 (silent terminal timeout):** fixed; verified in both cases.
- **R1-F6 (planning-doc hygiene: TEST_RESULTS references a FINAL_REPORT
  that does not exist; LOOP_LEDGER not advanced):** still open — no
  FINAL_REPORT.md in `.planning/zcode-r23-C/` and the ledger still ends at
  round 5 "RUNNING". P3, pre-PR housekeeping.
- R1's CMake analysis (PRIVATE static-lib `$<LINK_ONLY>` propagation;
  `src/data/CMakeLists.txt:107-110` already carrying the geospatial
  objects in final links) matches my independent derivation. R1's claim
  that `git diff 57df09e502..HEAD` on the batch pair was EMPTY was true at
  R1's review point (662799a134); the R1-fix commit c875659c00 then added
  only the comment/header rewording — my re-check of the same range finds
  exactly those doc deltas and nothing else. Consistent.
- Nothing R1 flagged as fixed regressed: WP-1 fix intact (empty diff
  fb1b2e8489→HEAD on task_center.cpp), WP-2 restored content
  byte-identical to the original fix modulo comments, tests green 5x.
- **What R1 missed:** the restore-failure and concurrent-publish corners of
  the "never disagrees" claim (R2-F1), the silent-skip success shape
  (R2-F2), the stale `m_forwardedLogCounts` doc (R2-F3), the
  accepted-loss-window comment gap (R2-F4), and the untranslated-catalog /
  "entry(ies)" translator-hostility note (R2-F5 — R1 verified the .ts
  state but read it as "no regression surface" rather than a follow-up).
