# ADR 0119: Review Remediation Round 2 (Slices 52–60)

## Context

A six-domain Qt review (qt-cpp-review skill, parallel agents) over slices
52–60 surfaced deterministic defects plus one pre-existing architectural
issue that slice 54's `get_lineage` made reachable. This ADR records the
fixes; the review's investigation targets that stayed open are listed at the
end.

## Decision — fixed

1. **Batch dialog `m_qgisContext` leak** — bare `new QgsProcessingContext()`
   never freed (no destructor). Now a `std::unique_ptr` member with an
   explicit `~BatchProcessingDialog`.
2. **Batch dialog initial algorithm had no parameter section** — the first
   `addItem` fired `currentIndexChanged` before the widgets existed and the
   guard swallowed it. `setupUi()` now calls `updateAlgorithmParameters()`
   once at the end.
3. **`collectParamOverrides()` rebuilt per batch item** — moved out of the
   `onRun` loop (the form is frozen during the batch).
4. **`runBatchItem` LongLong/UInt overrides truncated via `toInt()`** —
   dispatch by `QMetaType` (LongLong/ULongLong → `Json::Int64`).
5. **Vector algorithms always wrote `_processed.tif`** and failed — the
   output extension now follows the OUTPUT parameter type (`vectorDestination`
   → `.gpkg`, else `.tif`).
6. **`percentileThreshold` p=0 underflowed to the maximum** (rank `size_t`
   wrap) — clamped so p=0 yields the minimum; now reachable from the dialog
   percentile spin (slice 58). Added a regression test.
7. **Statistical-threshold guard was vacuous** (`ChangeStats::count` held
   total samples, not finite ones) — `ChangeStats` gains `validCount`;
   the branch reuses the outer statistics pass (removing a full second scan)
   and falls back to the manual threshold with a warning when fewer than two
   varying finite values exist (identical/NaN inputs no longer flag the whole
   raster as changed). Regression test added.
8. **Legacy `change_mask` silently dropped cleanup/MMU** (backend skips them)
   — the dialog now disables the cleanup and MMU controls for that method,
   matching the already-forced manual strategy.
9. **`connectedComponentFilter` int-wrapped indices past 2^31 pixels** — the
   kernel refuses such rasters (returns false → caller error) instead of
   out-of-bounds access.
10. **MCP output commits ran on a detached thread and were rejected by the
    Data Manager's owning-thread guard, rolling back (deleting) the published
    output** — the completion watcher now routes payload construction (which
    commits) to the Data Manager's thread via `BlockingQueuedConnection` on a
    dispatcher-owned bridge `QObject`. Pre-existing defect (TICKET-23 commit
    path), but `get_lineage` (slice 54) made the silent output loss directly
    observable. `dispatchAndAwait` — the only path that could deadlock with
    blocking — has no callers. Verified by the existing committer suite plus a
    new owning-thread `OutputCommitter` registration test (the headless test
    harness's event loop does not pump cross-thread queued invocations, so the
    watcher route itself cannot be exercised in tests).
11. **`apply_mask` dropped the dataset-level radiometric state** — it now
    carries `SICNU_RADIOMETRIC_STATE` to the output, keeping the ADR 0114
    comparability chain intact across calibration → mask → change detection.

## Remaining investigation targets (not fixed this round)

- DataManager read-side access has no mutex (safe today only via the
  owning-thread topology); adding locks or documented read contracts would
  harden it for background provenance services.
- `runBatchItem`'s QGIS branch still lets `alg->run` exceptions propagate to
  the public API (the batch loop catches them); an internal try/catch would
  match the RS branch's contract.
- The headless test harness does not process cross-thread queued events, so
  watcher-route integration tests are not possible headlessly.
