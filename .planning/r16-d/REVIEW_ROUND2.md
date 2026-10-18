# R16-D Independent Review — Round 2

Reviewer: second fresh subagent (no implementation involvement). Scope: regression/boundary verification of the R1 fix commits + full-diff re-sweep.

## Verdict at review time: 1 NEW P1 / 1 P2 (evidence) / 3 P3 — R1 dispositions all FIXED-VERIFIED except the accepted/noted P3s

## Findings and dispositions

| Sev | Finding | Disposition |
|-----|---------|-------------|
| P1 (new, introduced by R1 fix) | `confirmSaveEdits` discard paths dereferenced `vl` after `rollbackEditLease` — the R1 `retryPendingRelocations` made the rollback able to relocate (and destroy) the layer synchronously (same UAF class as R1's P1, on the sibling path) | FIXED in 259cc435d: both rollback fall-throughs snapshot the id and re-resolve; regression test drives the real `onLayerTreeClicked → confirmSaveEdits` path over a pending relocation (layer relocated, no crash, replacement non-editing) |
| P2 (evidence) | no post-R1-fix GREEN run recorded in track docs | FIXED: post-fix GREEN×2 recorded here and in TEST_RESULTS.md — T07 track (honesty 152/11, display 6490/25, both rounds exit 0) and integration (all 7 families ×2, below) |
| P3 | failed retry permanently dropped the pending marker (erased before attempting) | FIXED in 259cc435d: marker cleared only at the success return |
| P3 | `confirmSaveEdits` not-modified path also exposed to the P1 (rollback no-op but lease release still retries) | covered by the same fix + the regression test (which drives the not-modified path) |
| P3 (pre-existing, surfaced) | deleting the editing layer mid-session with a pending relocation: the next assetChanged re-materializes a layer the user deleted (null-QPointer skips the editable check) | ACCEPTED — pre-existing relocateLayer semantics; noted for the display-manager owner |
| P3 (noted, file owner) | `mapspec_compiler.cpp:1224` binding.mode isMember-only guard (file untouched by this PR) | carried in the PR body as an open item |
| P3 (accepted, from R1) | itemPageSize fallback beyond declared pages; whitespace 10-page cap; atlas page-0 filter drops unmanifested >0-page content | accepted (documented) |
| Docs | R1 header counts vs table; T07 ledger/TEST_MATRIX/IMPLEMENTATION wording pre-dating the R1/R2 corrections; OWNERSHIP missing main_window_connections.cpp; WORK_PACKAGES WP-T08-8 phrasing | all corrected in this commit |

## Verified-fixed in round 2 (no action)
commitEditLease snapshot; Save All id-iteration; sibling read-only reset via lease assetId (teardown order safe); retryPendingRelocations mechanism (thread affinity, iterator safety, re-entrancy, no lease-release recursion); chart_registry full sweep (all remaining coercions fronted by validation / internal / null-tolerant); geometry table all 8 entries vs vendored tools; degenerate-bbox refusal consistent with the pre-existing checkExtent fail-closed contract; atlas test fully synchronous (no flake window); lease test's only wait is the auto-display event flush (fails loudly if starved).

## Final status after this round
0 P0 / 0 P1 open. Post-R2-fix GREEN×2: T07 track families (152/11 + 6490/25 ×2) and integration full set below.
