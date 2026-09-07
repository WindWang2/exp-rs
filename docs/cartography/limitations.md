# Documented Limitations of QGIS-Backed Cartography Rendering

MapSpec compiles to `QgsPrintLayout` and QGIS renders it (ADR 0127). The
declarative layer inherits QGIS's semantics; these limits are documented
divergences or deferred capabilities, not hidden gaps.

Platform 5.0 status: the locator, atlas, conditional-visibility and
layer-symbology gaps of 4.0 are closed (see below); the remaining entries are
honest QGIS/toolchain boundaries.

## Layout & items

- **Locator insets are real now**: `inset_maps[].locator.target` compiles to
  the QGIS map-overview extent indicator (outline/region/frame styles) with
  an optional caption. *Remaining nuance:* connector lines from the inset to
  the main map are not drawn; nested locators resolve breadth-first with a
  depth cap of 2 (`locator` of an inset that itself is another inset's
  target is supported, deeper chains are ignored).
- **Atlas is usable**: coverage layer, filter, sort, filename expression,
  margin fraction and page numbering compile to `QgsLayoutAtlas` (see
  atlas-guide.md). *Remaining nuance:* per-feature **symbology overrides**
  stay QGIS-native configuration through `layout:*`/`symbology:*` tools —
  MapSpec declares renderer knowledge (`style_ref`/StyleSpec), not per-feature
  renderer rules.
- **Legend columns disable auto-update**: an explicit `columns > 1` takes the
  legend out of QGIS auto-update mode (QGIS semantics); entries then come
  from explicit patches, not the live renderer.
- **z_index spans the whole layout**: z-values apply to QGIS items globally,
  not per page.
- **Conditional visibility is compile-time**: `visible_if`/`content_if`/
  `page_if` are bounded declarative expressions resolved once against the
  stamped `condition_context` at compile. They are not render-time rules and
  cannot reference layer features.

## Text

- **Single-line text model in preflight**: the overflow estimator treats
  item text as one line (newlines start new lines; no word-wrap
  simulation). Multi-line labels render via QGIS with its own wrapping —
  the estimator checks the widest line only. Table-style charts estimate
  row heights with a leading factor and can auto-grow (`MAP_CHART_OVERFLOW`).
- **Platform font variance**: token fallbacks register Qt substitutions,
  but glyph shapes/widths vary by platform. Text-overflow preflight uses a
  platform-independent estimator, and golden pixel comparison is opt-in
  with generous tolerance (see visual-regression.md).

## Charts

- Rendered chart/colorbar PNGs are written per compile to a session temp
  directory (`$TMPDIR/sicnu-cartography-<layout>/`), layout-scoped so
  layouts cannot clobber each other's pictures; the OS tmpdir reaper bounds
  accumulation, exp-rs does not garbage-collect them itself.
- Inline charts render through a QPainter path (no QtCharts):
  `bar | line | pie | histogram | area | scatter | stacked_bar | grouped_bar |
  matrix | metric | table | summary_table | topn_table | sparkline`. Native
  `QgsLayoutItemChart` (vector_expression) supports the QGIS bar/line plot
  family only; everything else binds through the inline path.
- Matrix charts cap at 24×24 classes; inline bindings cap at 256 points;
  table charts render at most 64 rows with an explicit "… + N more" row
  (topn_table sorts and caps at `style.top_n` before that).

## Style

- **Dark print is experimental**: `scientific-dark` targets screen
  delivery; the print variant whitens background/text only — full dark
  print styling is not guaranteed against paper/Journal constraints.
- **StyleSpec is knowledge, not a renderer**: it compiles to QGIS renderer
  primitives (style-spec-reference.md). QGIS-native symbology remains the
  authority; editing a renderer by hand after `style:apply` is not tracked
  back into the StyleSpec (one-way compilation).
- **Conditional `style_ref` application**: layer `style_ref` fields resolve
  through the style tooling; unresolved style ids are reported (advisory),
  never fatal to the compile.

## Deferred (Platform 5.0)

- Per-glyph text measurement, word-wrap-aware overflow (single-line
  estimator + row heuristics remain).
- Interactive swipe/paired representations — synchronized before/after
  frames on static output.
- Chart dual-axis and per-point annotations — evaluated, deferred until a
  deterministic rendering contract exists.
- Atlas per-feature symbology overrides through declarative style specs.
