# Migrating MapSpec documents to v5 (Cartography Platform 8.0)

v5 is a **strict superset of v4**: every new field is optional, no existing
field changed meaning, and documents with `spec_version ≤ 5` validate and
compile. `upgradeMapSpec` re-stamps older envelopes to 5 unchanged.

## New surface

### 1. Output declarations (envelope level)

```jsonc
{
  "spec_version": 5,
  "output": {
    "formats": ["png", "pdf"],   // closed vocabulary: png | pdf; ≤ 8 entries
    "dpi": 300,                  // number in [72, 1200]
    "dir": "exports"             // optional; non-empty string when declared
  }
}
```

Semantics: a *delivery declaration*, not an action. Compilation never
auto-exports; `cartography:compose` echoes the block as `declared_output`
and the Harness `confirmMapOutput` copies it into the map confirmation so
the delivery contract travels with the verification verdict.

### 2. Typed data bindings (per item)

`binding` remains an object. v5 shape-validates it (diagnostics, not
semantic changes):

- `mode`: non-empty string;
- `layer`, `field`, `expression`, `x_expression`, `y_expression`,
  `filter`: strings;
- `params`: object;
- `data`: array, ≤ 256 entries (the inline chart budget);
- `matrix`: `{labels: [n], rows: [n][n]}`, square, ≤ 24×24.

No key is removed or renamed; shipped templates keep validating.

### 3. Locator connector graphics

```jsonc
{ "id": "inset-1", "rect_mm": [...],
  "locator": {
    "target": "map-1",
    "connector": { "style": "dash", "stroke_mm": 0.6, "color": "#0072b2" }
  } }
```

`connector` (object; all fields optional) compiles a QGIS-native polyline
(`QgsLayoutItemPolyline`, item id `<inset-id>-locator-connector`) from the
inset frame edge toward the referenced frame's projected extent anchor.
Geometry is a pure function of the declared rects/extents; without
resolvable extents the anchor is the frame center.

### 4. Declared typography (text items)

```jsonc
{ "id": "title-1", "text": "……",
  "font": { "break_policy": "halfwidth", "line_height": 1.4 } }
```

- `break_policy`: `none` (default, 7.0 model) | `halfwidth` (line-final
  fullwidth closing punctuation measures half advance in the wrap-aware
  overflow rule);
- `line_height`: 0 < h ≤ 3 leading override for the wrap-aware rule.

### 5. NoData legend entries

`legends[].nodata = {label}` compiles a swatch composite (QGIS shape
rectangle + label) inside the declared legend rect. The
`MAP_NODATA_LEGEND` preflight rule (repairable) stamps the field from the
referenced style's `raster.nodata.label` when a style declares nodata and
the legend does not mention it.

## Migration steps

None required. Optionally: stamp `"spec_version": 5` (or run
`upgradeMapSpec`), declare `output` where the delivery contract is known,
and add `locator.connector` / `font.break_policy` / `legend.nodata` where
the new capabilities are wanted.
