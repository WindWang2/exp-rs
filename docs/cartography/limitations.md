# Documented Limitations of QGIS-Backed Cartography Rendering

MapSpec compiles to `QgsPrintLayout` and QGIS renders it (ADR 0127). The
declarative layer inherits QGIS's semantics; these limits are documented
divergences or deferred capabilities, not hidden gaps.

## Layout & items

- **Locator insets are plain frames**: `inset_maps` compile as secondary
  map frames with their own extent; the classic locator *outline box*
  (main-map extent drawn inside the inset) is not generated. Insets share
  the main frame's extent when no explicit extent is given, so the
  outline is the full extent — visually a zoom-parity inset.
- **No atlas beyond the hook**: `page.atlas` configures enabled state,
  coverage layer and filename expression. Feature sorting, filtering,
  and per-page feature-driven symbology remain QGIS-native configuration
  through `layout:*` tools.
- **Legend columns disable auto-update**: an explicit `columns > 1` takes
  the legend out of QGIS auto-update mode (QGIS semantics); entries then
  come from explicit patches, not the live renderer.
- **z_index spans the whole layout**: z-values apply to QGIS items
  globally, not per page.

## Text

- **Single-line text model in preflight**: the overflow estimator treats
  item text as one line (newlines start new lines; no word-wrap
  simulation). Multi-line labels render via QGIS with its own wrapping —
  the estimator checks the widest line only.
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
  `bar | line | pie | histogram | area | scatter | stacked_bar | matrix |
  metric`. Native `QgsLayoutItemChart` (vector_expression) supports the
  QGIS bar/line plot family only; pie binds through the inline path.
- Matrix charts cap at 24×24 classes; inline bindings cap at 256 points.

## Style

- **Dark print is experimental**: `scientific-dark` targets screen
  delivery; the print variant whitens background/text only — full dark
  print styling is not guaranteed against paper/Journal constraints.
- **Token coverage**: tokens parameterize label text, charts, colorbars,
  margins and solver defaults. QGIS-native symbology of map *layers*
  (renderers) lives in the symbology domain (`symbology:*` tools), not in
  MapSpec tokens.

## Deferred (by ADR 0131)

- Rule-based conditional item visibility at compile time (conditionals are
  template-instantiation-time today).
- Per-glyph text measurement, word-wrap-aware overflow.
- Swipe/paired interactive representations — represented as synchronized
  before/after frames on static output.
