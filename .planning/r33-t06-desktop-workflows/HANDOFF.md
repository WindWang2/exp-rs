# R33-T06 — CROSS-TRACK HANDOFF

Each item states the exact path, the actual vs expected contract, the owner, the
minimal input and the evidence.

## H-01 → T04: i18n backend text (issue #2108)

* **Paths.** `src/science_context/capability_router.cpp` (66 `nonui_zh`, cap 4),
  `src/teaching/lab_feedback_projection.cpp` (14, cap 13),
  `src/teaching/lab_session_state.cpp` (21, cap 15).
* **Actual.** `audit_i18n_strings.py --gate` fails those three per-file caps at
  this HEAD; the global ceiling fails because of them (`nonui_zh=1095 > 1059`).
* **Expected.** Either migrate the user-visible subset to `tr()` with English
  sources (the real fix) or record a reviewed cap bump *per file* in
  `tests/fixtures/i18n_string_baseline.json` with a justification. This track
  may not move the baseline (brief: "禁止直接升 baseline").
* **Owner.** T04 (backend text) + the i18n owner for the baseline file.
* **Evidence.** BASELINE.md "Reproduction of the inherited i18n gate".

## H-02 → #2134 / T06-after-merge: `sicnu_zh_CN.ts`

* **Path.** `resources/translations/sicnu_zh_CN.ts` — **#2134-exclusive**.
* **Actual.** This track added **no** `tr()` source and no TS context, so
  nothing has to be extracted for it.
* **Expected.** If a later WP adds user-visible text, it must hand over exact
  `context / source / translation` triples rather than edit the TS.
* **Also.** Issue #2081 (freshness gate 5 FAIL + `test_bilingual_delivery_ui`
  reading English) cannot be closed from here: the TS is held by #2134 and
  `tests/test_bilingual_delivery_ui.cpp` is **T05's**.

## H-03 → T06 next session: cartography draft vs compiled layout (WP08)

* **Path.** `src/app/cartography/cartography_dock.cpp`.
* **Actual.** `adoptSpec()` (537-545) unconditionally does
  `m_composedLayoutName = spec["layout_name"]`. `runCompose()` (566-569) calls
  `adoptSpec()` **before** reading `compiled`, so a compose that reports
  `compiled=false` still leaves a non-empty `m_composedLayoutName`. Every
  consumer keys off that single string: the Export/Designer enable gate (496,
  511-521), `runExport()` (648, 662), `runDesigner()` (739-752) and
  `runRepair()`'s post-adopt. Result: the UI offers "compiled layout" actions
  for a layout that never compiled, and a stale name survives a failed compose.
* **Expected.** Separate *current draft spec*, *compiled layout name (with the
  spec revision it was compiled from)* and *last delivered artifact*. Clear the
  compiled name on a failed/aborted compose, and re-derive it only from a
  result whose `compiled` is true.
* **Minimal input.** The `cartography:compose` result object: `{ mapspec,
  compiled, layout_name }` (see the `runCompose` callback).
* **Test.** `tests/test_r25_d_cartography_dock.cpp` already drives the dock;
  add a case "compose reports compiled=false → Export and Designer disabled,
  status names the failure".

## H-04 → T06 next session: multi-page export must not damage the previous
delivery (WP13)

* **Path.** `src/agent/cartography/export.cpp`.
* **Actual.** On a per-page failure the code removes every path in `delivered`
  (286-287, 335-336, 395-396) — correct for the pages written by *this* run, but
  the brief describes a case where the new run overwrites the same final paths
  that a previous successful generation owns. Whether the previous bytes
  survive therefore depends on ordering that has not been reproduced.
* **Expected.** A failed B generation leaves A's bytes + manifest intact and
  never leaves a mixed-generation directory.
* **Minimal input.** An existing A export (two pages), then a B run whose
  second page is forced to fail.
* **Reuse.** `src/geospatial/util/atomic_fs.*` (T03) for the writes; do not add
  a second atomic-write helper and do not touch the public output committer
  (T04).

## H-05 → T06 next session: history panel truncation label (WP16)

* **Path.** `src/app/workbench/processing_history_model.cpp:30-42`.
* **Actual.** `m_droppedCount += sorted.size() - kMaxRows` accumulates across
  refreshes. That **is** the existing contract (a second `setEntries` with 2000
  rows must give 4000), and
  `tests/test_r30_t06_history_action_parity.cpp` pins it. The risk is the UI
  header wording: if it reads as "the current snapshot was truncated", the
  number is a lie after the second refresh.
* **Expected.** Keep the cumulative API; if the label is ambiguous, add a
  *separate* current-snapshot field rather than zeroing the accumulator.

## H-06 → T06 next session: Studio sensitivity chart truth (WP17)

* **Path.** `src/app/experiment_studio/experiment_studio_dock.cpp:2106-2132`.
* **Actual.** (a) `curves.first()` — parameters/metrics after the first are
  unreachable; (b) `p.value("mean").toDouble()` on a missing/non-numeric key
  yields **0.0**, so a missing metric is drawn as a real zero; (c) a failed
  report parse leaves `m_lastSensitivityVm` from the previous run, and
  `rebuildChart()` then repaints the OLD curve under the new title.
* **Expected.** Select a curve by parameter/metric; refuse a point whose
  `mean` is not a number (gap, not zero); clear the view model on a rejected
  report so A's curve cannot stand in for B.
* **Constraint.** `src/experiment_studio/**` is this track's; the *report
  producer* (`src/experiment/evaluation.*`) is T01's — do not recompute
  statistics in the UI.

## H-07 → T03: collection paging backend cost (WP06)

* **Path.** `src/app/panels/data_manager_panel.cpp` (paging over a full
  `CollectionSnapshot`).
* **Actual.** Paging is client-side over the whole snapshot; the UI must not
  claim `O(page)` backend cost. If a real catalog needs server-side paging, the
  contract change belongs to T03 (`src/data/**`), not to the panel.
