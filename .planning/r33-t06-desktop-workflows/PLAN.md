# R33-T06 — PLAN / WORK-PACKAGE STATUS

Status vocabulary: `DONE` (oracle satisfied with evidence in ACCEPTANCE.md),
`ALREADY_DONE_WITH_EVIDENCE`, `PARTIAL`, `BLOCKED`, `NOT_EXECUTED`.

| WP | Scope | Status | Note |
|---|---|---|---|
| WP01 | New-master residual clearing | **PARTIAL** | Baseline, open PRs/branches/issues re-read (BASELINE.md, PR_BRANCH_MATRIX.md, ISSUE_TRIAGE.md). Merged #2131/#2132/#2133 recorded as inherited. |
| WP02 | Preview A→B file generation identity | **DONE** | Key captured at request; cache write gated on identity stability; `Result::identityChanged`. Test: replaced file mid-render. |
| WP03 | Preview non-finite / per-band NoData | **DONE** | ±Inf/NaN are display misses; `computeRange` finite-only; `scaleToByte` non-finite guards (removes float→quint8 UB). Tests: Float32 specials, Float64 −Inf, all-missing, flat, per-band NoData. |
| WP04 | Queue / cancel workload bound | **DONE** | Shared abort flag + immediate registry removal on supersede/cancel; `pendingRequests()`. Test: 1000-request burst keeps ≤1 pending. |
| WP05 | Preview lifetime / teardown | **PARTIAL** | Teardown-with-work-in-flight test added; cache-hit/async/dead-receiver already covered by `test_asset_preview_service`. Project switch and standalone-layer thread affinity not exercised in this session. |
| WP06 | Collection paging pressure/identity | **NOT_EXECUTED** | Static read of `DataManagerPanel::addCollectionPagerRow`/`renderCollectionPage` only; no defect confirmed yet, needs its own fixture + `test_data_manager_panel` build. |
| WP07 | Container members / batch recovery UI | **NOT_EXECUTED** | Needs `test_r28_t04_import_batch` + import fixtures. |
| WP08 | Draft vs compiled layout separation | **NOT_EXECUTED** | Static evidence recorded (HANDOFF H-03): `adoptSpec()` sets `m_composedLayoutName` from an *uncompiled* spec (cartography_dock.cpp:537-545) and every Check/Repair/Produce reads that same name (496, 648, 662, 739) — the draft/compiled split the brief describes. |
| WP09 | Active view / layer instance / symbology | **NOT_EXECUTED** | — |
| WP10 | Designer round-trip / project lifecycle | **NOT_EXECUTED** | — |
| WP11 | Multi-page preview budget | **NOT_EXECUTED** | — |
| WP12 | Quality report locatability | **NOT_EXECUTED** | — |
| WP13 | Multi-page/atlas failure keeps old delivery | **NOT_EXECUTED** | Static evidence recorded (HANDOFF H-04): `export.cpp:286-287, 335-336, 395-396` remove `delivered` pages on failure; whether that can touch a *previous* successful generation needs the A→B fixture. |
| WP14 | Manifest vs actual delivery | **NOT_EXECUTED** | — |
| WP15 | Delivery result actionability | **NOT_EXECUTED** | — |
| WP16 | History panel counting/artifact identity | **NOT_EXECUTED** | Static evidence recorded (HANDOFF H-05): `processing_history_model.cpp:33` `m_droppedCount += …` (cumulative by contract); UI label must not read as "current truncation". |
| WP17 | Studio sensitivity curve truth | **NOT_EXECUTED** | Static evidence recorded (HANDOFF H-06): `experiment_studio_dock.cpp:2113` `curves.first()` (only the first curve), and `:2121-2122` `.toDouble()` on a missing key yields 0.0 — a missing metric is plotted as zero. |
| WP18 | Teaching/lab UI completeness | **NOT_EXECUTED** | — |
| WP19 | Bilingual / help / audit / visible desktop | **PARTIAL** | `hard_en` misclassification fixed in `scripts/audit_i18n_strings.py` (+2 selftest cases). Gate still red on three T04 backend files — inherited, not hidden. No `tr()`/TS work: `sicnu_zh_CN.ts` is #2134-exclusive. Native desktop/DPI/dark-mode acceptance not run. |
| WP20 | E2E closure + independent PR | **PARTIAL** | Independent review of the final HEAD is recorded in REVIEW_R1/R2; five E2E chains are not all executable in this session (see HANDOFF). |

## Why this PR is scoped to WP02–WP05 + WP19

The five E2E chains each need a different heavy target
(`test_data_manager_panel`, `test_r25_d_cartography_*`, `test_mapspec`,
`test_r30_t07_gui`, …). One configure of this tree took **27 min** and the two
preview targets alone pull `qgis_core` + `qgis_gui` + `Sicnu::Geospatial`.
Rather than land eight unverified edits across cartography/export/studio, this
delivery lands **four verified work packages with real GDAL/Qt fixtures** and
hands the remaining static findings over with file:line evidence (HANDOFF.md).
