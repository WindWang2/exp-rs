# Cartography Production 11.0 — async produce chain, atlas delivery, series, template governance & deterministic export evidence

> **Local evidence only; no online CI dependency.**
>
> **P0 (out of scope, minimal build-unblocks included):** `origin/master@a5b11b7f`
> fails to build on this host — (1) `src/agent/data_platform_tools.cpp`
> (PR #992/D19 artifact) uses `BenchmarkService` (`sicnu::experiment`)
> unqualified with no using-directive: **1-line build-unblock**, identical to
> open PR #1009's fix (dedupe: rebase takes whichever lands first); (2)
> `src/workflow/pipeline_run_coordinator.cpp` (PR #991 artifact) Win32 branch
> lacks `<fcntl.h>` (`_O_WRONLY/_O_BINARY`): **1-line include**, also
> identical to #1009's. Both are outside this track's scope and only unblock
> compilation of `sicnu_agent`/`sicnu_workflow`.

## Baseline & dedupe

- Baseline: `origin/master@a5b11b7f10fa010c1c060864fb427d777ba9a4aa` (rebased clean; no drift at PR time).
- Startup audit: #991/#992 were already merged INTO the baseline; open PRs at start were #1009 (execution runtime — business files zero overlap with this track, shared integration files append-only) and #1008 (spectral, CONFLICTING — no file touched by this track).
- Open issues #1001–#1007 (io/workflow/dataset/georef domains) deduped one by one: none are cartography; recorded OUT_OF_SCOPE in `.planning/cartography-production-11/BASELINE.md`.
- Local parallel worktrack `zcode/geoai-promptable-foundation-platform-11` writes only `app/lib/modelops/**` (Python) — zero overlap.

## Why

Platform 10.0 made compose/preflight/repair/export reachable from agent/CLI/GUI through one engine, but the audited production gaps were real:

1. CartographyDock ran operators **synchronously on the GUI thread** — no progress, no cancel;
2. **Atlas per-feature delivery did not exist** — the atlas-guide export claim had no backing code (zero atlas iteration in export.cpp);
3. Export **evidence did not persist** — the structural digest died with the return value; no manifest; a delivery could not be re-verified from disk;
4. Templates had **no version migration or lifecycle** (no analog of `upgradeMapSpec`) and component contracts (required furniture) had no enforcement;
5. Export had **no progress/cancel inside the render** and no honest environment record.

## What lands (WP A–H)

- **A — async production chain**: `cartography:produce` (6th cartography operator + 23rd agent tool + GUI "produce" button) orchestrates upgrade → validate → compose → bounded repair → export → manifest over the SAME engine free functions (zero duplication; hoisted shared `resolveCompositionPass`/`composeProvenance`). Typed refusal codes incl. `UNSAFE_LEGEND_AUTO_UPDATE` (render-hazard guard, see Known limitations); **atomic publish**: failure/cancel at ANY stage — including between export and manifest — rolls the delivery directory back to its pre-call content. CartographyDock submits every action to TaskCenter (the same registry dispatch a workflow node takes) with progress mirroring, busy gating and a Stop action.
- **B — atlas/series**: `exportMapAtlas` (inside the single export authority) delivers one atomic page per coverage feature — per-page sha256/feature_id/label/coverage-CRS extent, cancellable at every page boundary, 512-page runaway guard (engine-clamped), png-only honesty (pdf/svg on an atlas layout behave exactly like `cartography:export`, manifest `mode:"single"`); `planSeries` (table/vector) materializes multi-page MapSpec **v6** with per-page variables/extents/titles/page numbers/index page, `{{token}}` plan-time substitution, id-clone + reference remap (`map_ref`/`locator.target`/`constraints[].items`), and the engine page convention (body page = physical 0, `pages[k]` = physical k+1 — review P0 fixed with a compile-only page-count regression test). 10-page materialized cap states the atlas path instead of truncating.
- **C — template governance**: `descriptor_version` + idempotent v1→v2 load migration (required_furniture derived from slot roles); `deprecated`/`replaced_by` lifecycle stamped into drafts; `required_furniture` contract enforced by preflight `MAP_REQUIRED_FURNITURE_MISSING` with repair routed into the existing add_* furniture fixes.
- **D — layout quality**: new append-only rules `MAP_TINY_FONT_PRINT` (7pt floor at declared print dpi ≥ 300), `MAP_ALIGNMENT_DEVIATION` (same-width near-column x drift), `MAP_WHITESPACE_IMBALANCE` (one-sided furniture block: >3× opposite margin, >35% page width, >12 mm; frames/insets excluded from measurement AND shift so the repair converges) — each with bounded, ledger-logged repair. `MAP_LEGEND_TRUNCATION` was NOT added (duplicate of `MAP_LEGEND_DENSITY`).
- **E — data-driven provenance**: `composeProvenance.bindings` (mode/layer/field/expression per bound item, ≤64) flows into compose/produce output and manifests; map-anchored annotations (`map_ref` + `anchor` + styled leader) compile to a leader polyline (locator-connector geometry contract).
- **F — deterministic export evidence**: `export_manifest` atomic sidecar — canonical payload `manifest_digest` (environment block EXCLUDED so two hosts' honest descriptions never fork a byte-identical delivery's identity), per-page sha256/bytes, structural digest, provenance; `readExportManifest` re-verifies a delivery from disk alone.
- **G — headless E2E**: engine / agent tool / RSOperator+TaskCenter three-surface byte-equality (identical spec → identical PNG sha256; TaskCenter submit + waitForTask proves the pipeline/CLI path). Render-gated on this host (see Known limitations), enabled with `SICNU_CP11_RENDER=1`.
- **H — template corpus QA**: every shipped template instantiate→produce→manifest cross-verified (failures asserted empty; degradations reported honestly). Render-gated (host).

## Compatibility

- Strictly additive: MapSpec **v6** is a strict superset of v5 (`page`/`pages[].variables|series_row|crs`, role "index"); `upgradeMapSpec` idempotent; template governance members all optional; v1 catalogs migrate at load; rule codes append-only; the existing 5 operators / 22 tools keep their semantics (the shared-pass hoist is byte-identical — reviewer-verified), with one documented additive output change: compose/produce now carry `provenance.bindings`.
- GUI: the dock no longer executes operators on the GUI thread (the only behavior change; engine semantics unchanged).
- Existing suites: `test_mapspec` (visual-render suite excluded, see below) 211/213 — the 2 failures are differential-proven pre-existing host conditions (below); `test_platform7` version pin updated 5→6 per that test's own strict-superset rule.

## Local tests (MSVC/Ninja dev-default, QT_QPA_PLATFORM=offscreen, ctest -j1, ninja -j2 cap; dual run — two consecutive identical executions)

| Suite | Result RUN1 | Result RUN2 |
|---|---|---|
| test_cartography_production_11 `[cp11]` (new, 19 cases) | 15 passed / 4 skipped (render gate) / **0 failed** — 142 assertions | identical |
| test_mapspec `~[visual]` (regression, 213 cases) | 211 passed / 2 failed (pre-existing host) — 1779 assertions | identical |

- **Render gate**: this host currently hangs INSIDE `qgis_core!QgsLayoutItemLegend::paint` for ANY legend-bearing layout render. Differential proof it is not this diff: the untouched master `test_mapspec [visual][determinism]` suite (renders 19+ shipped templates, legends included) hangs identically (cdb stack captured; >20 min, zero CPU progress; offscreen AND minimal platforms; parallel tracks ran the same suites green two days ago). Legend-free renders succeed. Render-dependent cases SKIP with `SICNU_CP11_RENDER=1` opt-in for healthy hosts; all non-render coverage runs everywhere (142 assertions).
- **Pre-existing rename failures (2)**: `test_platform9:1367` / `test_cartography_operators_10:139` fail at `QFile::rename` into `%TEMP%` ("cannot move the export into place"). Differential proof: `git stash` → pure-a5b11b7f rebuild → SAME tests fail with the SAME error → `stash pop`. `exportMapLayout`'s rename path is byte-identical to master (diff only ADDS the atlas functions).
- Resource evidence: `.planning/.../PERFORMANCE.md` + `monitor.log` (60 s RSS sampling; no loadavg under Git Bash — recorded; -j2 cap held, dropped to -j1 during a system memory crunch).

## Review findings & disposition

Two-axis review (read-only adversarial subagent over the full diff + main-agent self-review). **P0=1, P1=3 — ALL FIXED with regression tests** (series page-convention P0 with compile-only page-count test; manifest-stage cancel rollback; whitespace repair convergence — measurement set = movable set; post-repair render-hazard re-check with gate ordering that keeps every refusal code distinct). P2: 5 fixed (dock connection lifetime, engine-level 512 clamp + manifest write cap, failure-envelope artifact clearing, catalog/doc threshold sync, garbled doc repair), 1 dispositioned with docs (rename-over = commit point; rollback removes replaced files — no partial files ever remain). P3: 2 fixed ("Page N" label, test layer cleanup by id), 3 dispositioned/recorded. Full table: `.planning/cartography-production-11/REVIEW_LOG.md`.

## Known limitations

- Atlas delivery is png-only on this QGIS build (pdf/svg atlas layouts = static all-pages exports, honest `mode:"single"`).
- Materialized series cap at 10 pages (document `pages[]` cap); larger feature-driven products → atlas path, stated not truncated.
- Render-dependent E2E/corpus/atlas-delivery tests are host-gated here (`SICNU_CP11_RENDER=1`); they carry the strongest guarantees and MUST run on a healthy host/CI.
- No new CLI verb (headless reaches produce via `--pipeline` cartography:produce steps; direct verb is a follow-up).
- Atlas pages don't collect per-page font-substitution diagnostics (single-page path unchanged).

## Follow-ups

- Dock multi-page/atlas preview navigation.
- `cartography:produce` direct CLI verb + help/catalog/drift sync.
- Per-page font diagnostics in atlas manifests.
- Index page as a table-chart component (currently a bounded label).
- Host render-path investigation (legend paint loop) — tracked in EVIDENCE with stack captures.
