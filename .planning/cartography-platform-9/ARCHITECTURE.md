# ARCHITECTURE — Cartography Platform 9.0 (authority map & design)

## Authority boundaries (unchanged, enforced)

- **QGIS** is the only map/layout rendering authority. MapSpecCompiler
  translates MapSpec → `QgsPrintLayout` through LayoutService; nothing in
  this track renders maps itself. Charts/tables/colorbars are the sanctioned
  QPainter-derived exceptions (QPainter→PNG→`QgsLayoutItemPicture`), a
  pattern shipped since 4.0 — 9.0 keeps them inside the same boundary.
- **MapSpec** (`src/agent/mapspec`) is the declarative document authority:
  envelope validation, bounded condition expressions, the compiler, the
  best-effort extractor.
- **Cartography module** (`src/agent/cartography`) owns the bounded
  composition solver, preflight rules, deterministic repair, component/
  template/style/solution/chart registries, design tokens, typography
  model, and the agent tools.
- **Pi / Harness** decide when tools are invoked; tools are stateless and
  typed. No conversation loop lives in cartography (M10 adds only typed
  surfaces).

## 9.0 design decisions (per milestone, conservative-first)

### M1 solver 9.0 (composition.{h,cpp})
1. **Oscillation detection**: the solver already rolls back on budget
   exhaustion (#864). 9.0 adds *cause attribution*: a bounded per-constraint
   satisfaction-state history (last 2 passes is enough — a flip is a
   two-phase cycle) reported as `oscillation: <cid list>` in the
   non-convergence note and in a new `result.oscillations` surface. No new
   solve semantics; pure diagnostics.
2. **Convergence trace**: `result.trace` — per-pass {pass, moves,
   constraint cids} bounded by the existing 24-pass budget. Evidence, not
   behavior change.
3. **Text-driven sizing**: `fit_content` gains an optional
   `text_ref: <item id>` mode — content_mm is *computed* from
   `fitTextIntoBox` for the referenced text item (declared font, no wrap)
   at solve time. The solver stays geometry-only: the measurement uses the
   same deterministic typography model; no QGIS font metrics enter the
   solver (platform determinism preserved).
4. **Scoped re-solve (incremental)**: `resolveComposition` stays the single
   entry; a `resolveCompositionScoped(spec, itemIds)` wrapper restricts the
   runtimes to constraints touching `itemIds` (plus anchors/clamps for
   those items) — used by repair to keep later passes bounded. Same
   ordering rules → same fixpoint policy on the subset.
5. Unsolved-core search reuses the existing bounded simulation; the only
   change is the **duplicate-ledger** fix (see REVIEW_LOG P2-1).

### M2 multi-page / atlas (mapspec.cpp + mapspec_compiler.cpp)
1. `pages[]` entries may declare `furniture` (role-referenced items to
   repeat). Materialization happens at compile: for page p≥1 each
   referenced item is cloned (`<id>-p<p>`) with provenance
   `master_of: <id>`, positioned on that page; solver ignores clones
   (declared furniture is authoritative geometry).
2. Page-dependent text: `titles[].expression` / `labels[].expression`
   compile to QGIS-native label text via `QgsExpression` against the atlas
   feature when atlas is enabled; invalid expressions are a compile error
   (never silent-static text).
3. `page_break` constraint kind (`{kind: "page_break", items: [item]}`):
   moves the item to the next page (page+1 must exist or validation
   fails) — the deterministic, explicit cross-page primitive the repair
   can apply. `keep_with` chains across the break keep semantics (both on
   the new page).
4. `continuation` labels: declared block on a page-break target item
   compiles a label "§ continued" with resolved page number after final
   pagination (pure compile post-pass, deterministic).
5. Export page selection is a parameter of the M9 export tool (not a new
   format path): `pages: [n] | "all"`.

### M3/M4 component/template (registry.{h,cpp}, data/cartography)
1. Catalog audit-first (done — see CAPABILITY_MATRIX): no category gaps.
   New catalog entries only: `table--accuracy-matrix` (accuracy E2E needs a
   declared table) and accessibility fields on entries missing them.
2. Multi-parent `extends: [a, b]` — ordered, first-declared-wins per key,
   acyclicity validated at load (reuses the single-parent cycle walk over
   the first parent edge).
3. `cartography:diff_templates` — pure function over two resolved
   descriptors: added/removed slots, changed defaults (JSON-pointer
   paths), changed facets; bounded output.
4. Template provenance: `instantiateTemplate` stamps
   `template: {id, version}`; compose echoes it in provenance.

### M5 thematic (style_spec/style_compiler + data/cartography/styles)
1. `style.scale_ranges` (declared min/max scale + style override) compiled
   through the existing style compiler into QGIS scale-dependent renderer
   settings. Validation: min<max, finite.
2. Bivariate contract: `style.bivariate: {x_class: …, y_class: …}` is
   validated to reference two declared classifications with an explicit
   semantic contract string; without the contract, validation rejects
   (bivariate cannot be declared implicitly).
3. Change-map: documented style + template + E2E test (no renderer change).

### M6 charts/tables (chart_registry)
1. Deterministic numeric formatting helper (no locale grouping; explicit
   precision) adopted by all table/statistics renderers.
2. Dual-axis: declared `dual_axis: true` + two explicitly typed series
   groups; preflight rule fires when a chart declares two y-axes worth of
   series without the declaration.
3. Rule: non-inset furniture overlapping a map frame → new preflight code
   `MAP_FURNITURE_OVER_MAP` (repairable by reposition; reuses the overlap
   repair).

### M7 typography 3.0 (typography.{h,cpp})
1. Add per-line kinsoku *push-out* (a line may not END on an opening
   punctuation either — currently only line-start is guarded) —
   known-answer tests.
2. CJK golden render test + font-fallback diagnostic entry in export
   report (M9 hooks it).

### M8 QA/repair (quality.cpp)
1. New rules: `MAP_FURNITURE_OVER_MAP`, `MAP_DUAL_AXIS_UNDECLARED`,
   `MAP_SCALE_RANGE_INVALID`, `MAP_PAGE_FURNITURE_MISSING` (declared
   master furniture does not resolve), `MAP_CONTINUATION_INVALID`.
2. Repair ledger: `repairMapSpec` returns count (compat) and a new
   `repairMapSpecWithLedger` returns {applied, ledger:[{code, item_id,
   action, outcome}]}; the repair tool surfaces it.
3. Scale/CRS: map_frames may declare `crs` (validated "EPSG:xxxx" or WKT
   prefix); report-ish documents without any frame CRS →
   `MAP_FRAME_CRS_MISSING` (warning, non-repairable: CRS is a semantic
   fact the agent must supply).

### M9 export (new export.cpp in owned dir + tool)
1. `exportMapLayout(layout, {format, dpi, pages, dir})` → atomic
   (temp+rename), returns {path, sha256, bytes, dpi}; failure removes the
   temp file. PDF only when the QGIS build provides the PDF export path
   (honestly reported otherwise), SVG only if layout exporter supports it
   on this build — each capability probed and reported, never faked.
2. Missing-font/resource diagnostics: exporter report flags missing
   replacement glyphs through QFontMetrics substitution checks on the
   declared families.
3. Determinism: PNG render determinism test already exists; export
   metadata excluded from the digest inputs (only pixel/structural data).

### M10 harness (cartography_tools.cpp)
1. `cartography:explain` — bounded per-item/page answer: solver decisions
   for that item, its unsat cores, preflight findings for it.
2. `cartography:export` tool wired to M9; harness confirm stays the
   delivery gate (compose identity + export digest cross-checked).

## Non-goals (enforced)

- No second agent loop / scheduler / renderer / I/O layer.
- No conversation logic inside cartography tools.
- No unbounded caches/queues/threads (all existing registries stay
  mutex-guarded lazy singletons; export is synchronous and bounded).
- No new JSON envelope versions beyond additive optional fields (MapSpec
  stays v5 unless a milestone strictly needs v6 — current design needs no
  new required fields; multi-parent extends is template-descriptor level).
