# Qt model tester reentrancy evidence (PR #2143)

This probe has no exp-rs production dependencies. It is intentionally outside
normal test discovery: `auto`, `auto-insert` and `bad-delta` deliberately abort.
Do not classify an arbitrary crash as success: inspect the exact diagnostic.

```sh
cmake -S tests/probes/r33_t05_modeltester -B /tmp/r33-modeltester \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.0/gcc_64 -DCMAKE_BUILD_TYPE=Release
flock /tmp/exp-rs-build-j2.lock cmake --build /tmp/r33-modeltester --parallel 2
ulimit -c 0
/tmp/r33-modeltester/modeltester_probe manual
/tmp/r33-modeltester/modeltester_probe auto
/tmp/r33-modeltester/modeltester_probe auto-insert
/tmp/r33-modeltester/modeltester_probe bad-delta
```

## Source evidence

Pinned upstream source:
<https://github.com/qt/qtbase/blob/v6.8.0/src/testlib/qabstractitemmodeltester.cpp>

* Lines 237-244 connect `runAllTests` to `modelReset` / `rowsInserted` **before**
  the bookkeeping slots at 278-290.
* Lines 364-373 call synchronous `fetchMore` from `runAllTests`. `fetchingMore`
  prevents recursive *probes*, not the separately connected structural checks.
* Lines 814-815 require `changeInFlight == None` on rowsAboutToBeInserted.
  During the earlier completion-signal probe, it still contains the outer
  ModelReset / RowsInserted value; it is cleared only at 838-839 / 929-932.
* `setUseFetchMore(false)` (329-333, available since Qt 6.4) disables automatic
  population only. All structural, index, data, persistent-index and other
  official assertions remain attached, in Fatal mode.

## Actual local results: aqt Qt 6.8.0, Release, GNU C++ 15.2

| Mode | Exit | Diagnostic |
|---|---|---|
| `manual` | 0 | Contract-correct model, explicit synchronous fetches |
| `auto` | SIGABRT | line 814: changeInFlight=5 (ModelReset), expected None |
| `auto-insert` | SIGABRT | line 814: changeInFlight=6 (RowsInserted), expected None |
| `bad-delta` | SIGABRT | line 855: rowCount=3, announced count requires 2 |

The negative control uses the **same** Fatal tester and manual population
configuration as the repaired suite; it proves an actual row-count contract
violation is still rejected, rather than silencing failures. Successful manual
runs also emitted Qt's `Trying to construct an instance of an invalid type,
type id: 4097` warning; this is recorded, not filtered from the test logs.
No Qt 6.12 run was performed in this correction; no version-wide exemption is
needed or retained.

## Coverage restoration

`test_r33_t05_mission_paging.cpp` now attaches the official Fatal tester on
**every** project-supported Qt version (including >=6.12), before loading data.
Only automatic population is disabled. Existing tests explicitly exercise
reset, append, page-size raise, rebuild and fetch-to-tail paths while both the
Qt tester and RowContractWatcher observe all signals. Each explicit fetch also
checks its exact page delta, persistent index/data stability and upper bound.
No existing test or assertion was removed or relaxed.

Focused build of the repository's actual paging test and three production
sources, with the project's Catch2 3.7.1 version and Qt Core/Test, passed:
**5 cases, 2692 assertions**. This was an isolated lightweight harness, not a
full application build/CTest/CI run. The standalone negative modes above were
also executed and their expected diagnostics verified.

The restart E2E file was invoked with Node 24.22.1: both E3-A/E3-B reported
NOT_EXECUTED (missing host binaries), not passes. The fixture changes from
22d427dd are preserved: next_seq=3 after seq=2 and next_seq=4 after seq=3 match
`SessionJournal::fromJson`'s strictly-past-last-entry contract. No production
service or real user data was used.
