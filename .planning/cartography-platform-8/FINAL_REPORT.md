# FINAL REPORT — Cartography Platform 8.0

Branch `feat/cartography-platform-8` (base `origin/master` @ `2d4f0daedd`,
PR #836). Worktree `/home/kevin/projects/rs-studio/exp-rs-cartography-platform-8`.
Environment: Linux, system QGIS + Qt 6, Ninja/Release, gcc; builds ≤ -j3,
tests -j1. **Online CI/CD was not required and was not waited on** — all
evidence below is local and reproducible.

## Baseline & overlap audit

0 open PRs / 0 open issues at execution. Direct predecessor PR #832
(cartography 7.0); post-#832 merges are CI fixes only. Sibling 8.0 tracks
(execution-plane, geospatial-data-fabric, model-runtime, scientific-
processing, verification, dataset-mlops) verified non-overlapping with this
track's files. Capability matrix with per-item evidence:
`.planning/cartography-platform-8/CAPABILITY_MATRIX.md`.

## What shipped (verified gaps → commits)

- **A. Raster NoData wired into the QGIS renderer** (closes the 7.0 known
  limitation): `style:apply` pushes `raster.nodata` into the renderer —
  provider user-nodata range on the declared band + optional
  `QgsRasterRenderer::setNodataColor` shading (new `color`, default black,
  parseability validated; unparseable strings fall back honestly with an
  explicit problem). Idempotent; `buildRasterRenderer` mirrors the shading.
- **A. Locator connector graphics**: `inset_maps[].locator.connector`
  compiles to a QGIS-native `QgsLayoutItemPolyline` (new LayoutService
  `line`/`polyline` item type) from the inset frame edge to the referenced
  frame's projected extent anchor; north-up extent math (ymin at frame
  bottom), clamped, frame-center fallback, anchor-inside-inset suppression,
  compile-failure = compile error (never a silently missing feature).
- **A. Accuracy-summary component**: verified already complete on master
  (`accuracy--report.json`) — no action needed (overlap audit).
- **A. Desktop-capable PNG golden evidence**: `[visual][determinism]`/
  `[visual][golden]` verified end-to-end on Linux — real `QgsLayoutExporter`
  renders; ~10 golden scenes written and compared in tolerance; methodology
  recorded in `docs/cartography/visual-regression.md` (goldens stay
  out-of-tree by the documented design).
- **B. MapSpec v5** (strict superset): envelope `output` delivery
  declaration (png|pdf, dpi 72..1200, dir) validated + surfaced through
  compose/Harness (no auto-export); typed `binding` shape validation;
  `upgradeMapSpec` stamps v5; `docs/cartography/migration-mapspec-v5.md`.
- **C. Page-aware solver evidence**: `keep_with`/`avoid_overlap` refuse
  page-crossing pins with `page_overflow` reasons in unsatisfied/violated/
  decisions instead of silently writing off-page geometry; permanent-failure
  causes preserved verbatim in violation reasons; per-page height map
  matches the compiler's page model (verified by review).
- **D. Typography 2.0**: declared `font.break_policy`
  (`none | halfwidth` — line-final CJK closing-punctuation compression) and
  `font.line_height` leading override, consumed by the wrap-aware preflight
  rule; defaults reproduce 7.0 outputs byte-identically.
- **E/F. Component/template catalogs**: audited — 57 components + 56
  faceted/inheritable templates already cover the scope; no clones with
  conflicting semantics; no growth for its own sake.
- **I. NoData legend QA**: `MAP_NODATA_LEGEND` rule (repairable, converging
  repair stamping `legend.nodata` from the referenced style) + compiler
  swatch composite (QGIS shape + label); malformed declarations keep the
  rule firing and are shape-validated.
- **J/H. Compose identity + chart labels**: `cartography:compose` returns
  `structural_digest` + `provenance` + `declared_output`; Harness
  `confirmMapOutput` carries them (final-map confirmation identifies WHAT
  was composed); bar/histogram/grouped-bar labels elide deterministically.
- **Docs drift**: mapspec-reference "current: 3.0" header (code was v4),
  stale 5.0-era limitations text, nodata "future work" claims — all
  corrected; migration + reference + catalog docs synchronized; CHANGELOG.

## Test evidence (all local, reproducible)

- Baseline (pre-change master code, this host): cartography ctest selection
  **52/52 PASS** (60 s).
- `[platform8]`: **17 cases — ALL PASSED** (5.6 s): NoData provider/renderer
  wiring on a real synthetic GeoTIFF (idempotent re-apply), connector
  geometry (projected anchor, frame-center fallback, north-up asymmetry),
  v5 validation + migration, page-aware pins (refused + taller-page
  control, geometry-untouched assertions), halfwidth known-answer (5.5 em),
  nodata-legend rule + converging repair + swatch compile, compose identity.
- Full cartography sweep: **60/63 PASS**; the 3 non-passes are unrelated
  `_NOT_BUILT` executables (test_agent_golden_workflow, test_rpc_golden,
  test_rs_golden_e2e — harness/IO targets not built in this worktree,
  matched only by the `golden` name filter). Includes the PNG determinism
  and golden render cases.
- Harness regressions (plan_tools touched): test_harness_catalog (93
  assertions), test_harness_evals (263), test_agent_tools_3 (126) — ALL
  PASSED.
- Intentional pin update: platform7 version constant 4 → 5 (comment +
  migration doc explain the bump policy).

## Adversarial review & remediation

Two read-only subagents, full-diff scope, both verdict FAIL before
remediation. Combined: **0 P0, 3 P1, 5 P2, 15 P3**. All P1/P2 fixed (the
headline P1: the connector's extent→page Y projection was vertically
mirrored for north-up extents — caught because the reviewer noticed the
test only exercised the symmetric case; fixed with an asymmetric
known-answer test). P3s fixed or justified with rationale in
`.planning/cartography-platform-8/REVIEW_LOG.md`. Full re-run green after
remediation. **No unresolved P0/P1 findings remain.**

## Known limitations (honest)

- `pageBottomFor` uses the follower's declared page height; a pin between
  items on different pages compares a page-0-anchored y against the
  follower's page bottom (strictly stronger than 7.0; cross-page pins are
  an edge input).
- Page-aware evidence covers push-down pins (`keep_with`,
  `avoid_overlap`); top/left/right overflow remains `MAP_OFF_PAGE`
  preflight territory.
- Multiband nodata applies to the declared band only (QGIS per-band
  semantics; documented).
- NoData legend entries are composites inside the declared legend rect
  (QgsLayoutItemLegend cannot host custom nodes declaratively — documented).
- Chart label elide is pixel-path behavior guarded by the render-determinism
  hash; no dedicated unit test (widths provably positive).

## Performance / resource notes

See `.planning/cartography-platform-8/PERFORMANCE.md`. All new hot-path
costs are O(1) per feature application with no new passes, caches, or
registries; fixtures bounded (64-cell rasters, ≤24×24 matrices, ≤300-entry
arrays).
