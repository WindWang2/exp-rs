# PLAN — Professional Workbench 8.0

Milestones are ordered by verified gap size; each ends with a green targeted
build + tests before the next starts.

## M1 — SchemaForm 4.0 (package B; the 7.0 deferral)

1. Nested objects: `type:object` + `properties` → recursive sub-group;
   values()/setValues()/validate() nest; conditional visibility and advanced
   grouping compose with nesting.
2. Object arrays: `items:{type:object}` → repeatable item editors with
   add/remove; minItems/maxItems respected; round-trip stable.
3. Dynamic enum source seam: `x-ui-enum-source` + injectable provider
   (shell-provided); refreshChoices() re-queries providers; unknown source id
   degrades to free text + warning mark, never a silent drop.
4. Async value checks: `x-ui-check` (e.g. path_exists) on the shared bounded
   pool, generation-cancelled, warning-level inline marks.
5. A11y: accessibleName/Description on every editor, label buddies, focus
   order by schema order; advanced toggle keyboard-reachable.
6. Tests (headless QWidget, offscreen): nested round-trips, object-array
   round-trips + bounds, enum provider refresh + stale-choice upgrade,
   check cancellation on rebuild/destroy, validation parity (errors block,
   warnings don't), a11y names present.

## M2 — AssetPreviewService (package E)

1. Core service (GUI-free): raster thumbnail via RasterReader +
   readWindowResampled(Nearest) + byte budget + stretch-to-QImage; vector
   preview via bounded offscreen render; typed failures; LRU cache (count+
   bytes); generation cancellation; queued-signal completion.
2. Wire into DataManagerPanel detail pane (raster + vector assets), lazy on
   selection, cancel on selection change, teardown-safe.
3. Tests (headless): synthetic GeoTIFF/CSV fixtures via GDAL drivers;
   generation supersede; receiver destruction; cache bounds; failure paths;
   determinism/size bounds; scale evidence (timing at 10k×10k synthetic).

## M3 — AssetCatalogModel + DataManagerPanel swap (package D)

1. Lazy-fetch tree model over DataManager snapshots; incremental filter;
   truthful totals/truncation; deterministic selection preservation across
   refreshes.
2. Swap panel internals; keep public API; existing panel tests green.
3. Tests: 100k logical assets synthetic (bounded memory, timing evidence),
   filter, lazy fetch, selection preservation, truncation honesty.

## M4 — Context facts + suggested next action (package C)

1. ContextFacts: + hasInFlightTask (injected predicate), + selectedDataset/
   Run/Model counts (from existing push-selections), parity tests.
2. `suggestedNextAction(snapshot)` pure projection + palette/empty-state
   surfacing.
3. Tests: fact derivation matrix; suggestion table; disabled-command reason +
   suggestion co-render.

## M5 — Robustness + docs (packages A/H/I)

1. Stress tests for every new async seam (teardown/supersede/delete/switch).
2. Token/a11y sweep of new UI (no literal colors; accessible names).
3. docs/ui-architecture.md Part IV for 8.0 contracts; CHANGELOG entry;
   capability matrix updated; TEST_MATRIX.md + PERFORMANCE.md evidence.

## M6 — Adversarial review + remediation (2 subagents, track maximum)

Read-only review over the full diff: architecture/ownership/duplication;
correctness/edge cases; concurrency/lifetime/cancellation; performance/
boundedness; portability; security; backward compat; test vacuity; docs-vs-
code; GUI state; numerical validity where applicable. All P0/P1 fixed, P2
fixed or justified, P3 fixed or explicitly justified; affected tests re-run.

## M7 — Integration + PR

Sync master if needed; full-diff inspection; targeted + bounded sweep;
FINAL_REPORT.md; push; PR (no CI wait).
