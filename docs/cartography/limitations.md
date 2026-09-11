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
  an optional caption. *Platform 8.0:* a declared `locator.connector`
  compiles to a QGIS-native polyline (`QgsLayoutItemPolyline` via
  LayoutService) from the inset frame edge to the referenced frame's
  projected extent anchor — the classic locator relationship line; geometry
  is a pure function of the declared page rects/extents (with extents
  unresolved the anchor is the frame center). Nested locators (an inset
  targeting another inset) validate and compile — nesting depth is
  author-declared and not capped by exp-rs, so absurd chains render as
  absurd chains.
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

- **Deterministic width model, not font metrics**: the preflight model
  measures codepoints in fixed width classes (CJK one em, other 0.55 em,
  spaces 0.35 em) — deliberately font-free and platform-independent. Since
  7.0 the model includes word wrap, CJK kinsoku, line budgets and truncation
  policies (`MAP_TEXT_WRAP_OVERFLOW`); since 8.0 a declared
  `font.break_policy` (`none | halfwidth`) compresses line-final fullwidth
  closing punctuation and `font.line_height` overrides the leading used by
  the wrap check. Actual glyph shapes/widths still vary by platform — the
  estimator is the declared contract, not a raster oracle.
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
  back into the StyleSpec (one-way compilation). *Platform 8.0:* the apply
  path pushes `raster.nodata` into the QGIS renderer — the declared `value`
  becomes a provider user-nodata range on the declared band and
  `transparent: false` shades nodata pixels through
  `QgsRasterRenderer::setNodataColor` — closing the 7.0 validated-only gap.
- **NoData legend entries are composites**: `QgsLayoutItemLegend` cannot
  host custom nodes declaratively, so `legend.nodata` compiles as a swatch
  composite (QGIS shape rectangle + label) pinned inside the declared
  legend rect bottom — the same furniture class as charts/colorbars. The
  declared rect reserves the space; `MAP_NODATA_LEGEND` keeps declaration
  and rendering honest.
- **Conditional `style_ref` application**: layer `style_ref` fields resolve
  through the style tooling; unresolved style ids are reported (advisory),
  never fatal to the compile.

## Deferred (Platform 5.0)

- Per-glyph text measurement, word-wrap-aware overflow (single-line
  estimator + row heuristics remain). *7.0 shipped the wrap-aware engine;
  per-glyph font metrics remain deferred by design.*
- Interactive swipe/paired representations — synchronized before/after
  frames on static output.
- Chart dual-axis and per-point annotations — evaluated, deferred until a
  deterministic rendering contract exists.
- Atlas per-feature symbology overrides through declarative style specs.
