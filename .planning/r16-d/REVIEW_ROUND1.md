# R16-D Independent Review — Round 1

Reviewer: fresh subagent (no implementation involvement), full diff `origin/master...HEAD` (11 commits at review time). Adversarial checklist: API/contract, lifetime/concurrency, type safety, semantics, tests, cross-PR safety, deferral design, external-layer regression.

## Verdict at review time: 0 P0 / 1 P1 / 5 P2 / 7 P3

## Findings and dispositions (all P1 + all actionable P2/P3-table fixed in commits 3adca1967, 49a07a62c)

| Sev | Finding | Disposition |
|-----|---------|-------------|
| P1 | commitEditLease dereferenced `vlayer->id()` after `commitEdit` — the commit's synchronous `assetChanged` relocation may have destroyed the layer (UAF on the primary hardened path) | FIXED: identity snapshotted before the call (3adca1967) |
| P2 | checkUnsavedChanges "Save All" iterated a stale layer snapshot while each commit relocates sibling display layers | FIXED: iterate ids, re-resolve per iteration |
| P2 | releasing a removed layer's lease never reset siblings' read-only state (asset locked "edited in another view" forever) | FIXED: asset id recovered from the stored AssetLease; siblings reset |
| P2 | deferred relocation never retried after rollback (rollbackEdit emits no assetChanged); `pendingRelocations` was write-only; deferral diagnostic silently dropped | FIXED: `retryPendingRelocations(assetId)` called from every lease release + qWarning log; test drives the retry API (still defers while editing, proceeds after session end) |
| P2 | four remaining jsoncpp throw paths in chart_registry: numeric `title`, `parts[].label`, non-bool `diagonal_emphasis`, and the entirely-unguarded `bindNativeChart` path (validateChartSpec never fronts it) | FIXED: all type-guarded with fallbacks; numeric-title degradation asserted (49a07a62c) |
| P3 | geometry table over-strict: offset curve has a polygon mode; delete-part handles points | FIXED: table corrected + test expectations updated |
| P3 | itemPageSize falls back to body dims when item.page exceeds pages[]; whitespace loop's 10-page cap skips the 11th physical page | ACCEPTED (pre-existing caps; noted) |
| P3 | atlas page-0 filter silently drops legitimately-declared >0-page atlas content | ACCEPTED (those pages were never manifest-listed before — the filter only removes unmanifested junk; noted for a future per-page atlas contract) |
| P3 | mapspec_compiler.cpp binding.mode asString isMember-only guard (file untouched by this PR) | NOTED for the file owner |
| PASS | API/contract surface, teardown order (m_editLeases destroyed before m_projectContext), saveEdits re-resolution (displayLayerId collision-safe), relocateLayer re-entrancy, scaledenominator semantics vs raster sibling, P↔pages[P-1] convention consistency, tokenizer full-consume (no valid form newly rejected), test substance (no tautologies), cross-PR file safety (#1513–#1517 disjoint), external-layer behavior unchanged | — |
