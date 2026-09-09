# StyleSpec Reference (Platform 5.0)

StyleSpec is *declarative symbology knowledge*: it describes how a dataset
should be displayed. It is **not** a rendering engine — application goes
through QGIS renderer primitives (the adapter in
`src/agent/cartography/style_compiler.cpp`), and QGIS stays the single
rendering truth.

- Catalog: `data/cartography/styles/*.json` (17 shipped semantic styles)
- Registry: `StyleRegistry` (`src/agent/cartography/style_spec.h`)
- Application: `applyStyleSpecToLayer(layer, styleSpec)` / `style:apply` tool
- Embedded safety set: `style.landcover-classes`, `style.change-gain-loss`

## Document shape

```json
{
  "schema_version": "1.0",
  "kind": "style_spec",
  "id": "style.flood-extent",
  "version": 1,
  "description": "…",
  "applies_to": "raster|vector|any",
  "token_set_ref": "scientific-light",
  "semantics": ["flood", "water"],
  "raster":  { … },   // see below
  "vector":  { … }    // see below
}
```

## Token references

Any string value may be a `token:<dotted-path>` reference (for example
`"token:colors.water"`, `"token:palettes.diverging"`). References resolve
against `token_set_ref` (default `scientific-light`) at describe/apply time:

- `style:describe` returns a `resolved` preview plus `token_problems`.
- `style:apply` compiles the resolved values onto the layer.
- Unresolvable references are **reported, never silently kept** (this closes
  the 4.0 gap where `token:` strings leaked into `QColor` unparsed).

## Raster block

| field | values | notes |
|---|---|---|
| `renderertype` | `singleband_gray` · `singleband_pseudocolor` · `paletted` · `multiband_color` | closed vocabulary |
| `band` | 1-based integer | validated against layer band count at apply |
| `gamma` | > 0 | singleband_gray |
| `opacity` | [0, 1] | |
| `resampling` | `nearest`·`bilinear`·`cubic` | |
| `stretch` | `{type: minmax\|percentile\|stddev\|fixed, percentile, stddev, min, max}` | fixed needs min+max; percentile ∈ [0,50) |
| `classification.mode` | `discrete` · `continuous` | discrete → QGIS discrete ramp; continuous → interpolated |
| `classification.ramp` | palette name or hex array (token refs allowed) | used when `classes` absent; samples across stretch |
| `classification.classes` | `[{min, max, label, color}]` ≤ 64 | color = hex or token ref |

Renderer mapping: `singleband_pseudocolor` → `QgsSingleBandPseudoColorRenderer`
+ `QgsColorRampShader`; `paletted` → `QgsPalettedRasterRenderer` (classes keyed
by `min`); `singleband_gray` → `QgsSingleBandGrayRenderer` with
`QgsContrastEnhancement` stretch; `multiband_color` → `QgsMultiBandColorRenderer`
(`bands.red/green/blue`).

## Vector block

| field | values | notes |
|---|---|---|
| `renderertype` | `simple` · `categorized` · `graduated` · `rule_based` | |
| `field` | attribute name | required for categorized/graduated |
| `color` | hex / token ref | simple renderer fill |
| `symbols` | `{width_mm}` | line width / outline width / marker scale |
| `categories` | `[{value, label, color, symbol}]` ≤ 64 | graduated uses `min`/`max` per entry |
| `rules` | `[{expression, label, color}]` ≤ 64 | rule_based |
| `opacity` | [0, 1] | |
| `blend_mode` | `normal`·`multiply`·`screen`·`overlay`·`darken`·`lighten` | knowledge for map display |
| `scaledenominator` | `{min, max}` | QGIS scale-based visibility |
| `labels` | `{enabled, field, size_pt, color, halo_mm}` | `QgsVectorLayerSimpleLabeling`; size may be a token ref |

## Caps (boundedness)

64 classes / 64 categories / 64 rules / 16 semantics per style; documents are
skipped (and reported by `cartography:lint_catalog`) above 512 KB.

## See also

- `solution-authoring.md` — how solutions reference styles
- `docs/cartography/design-tokens.md` — the token sets being referenced

## Platform 6.0 — semantic applicability and token alias chains

### Applicability

A style may declare the data it is semantically FOR:

```jsonc
"applicability": {
  "value_domain": { "min": -1, "max": 1 },   // e.g. NDVI
  "band_count": { "min": 3 },                 // e.g. multiband_color
  "modalities": ["optical"],
  "semantics": ["ndvi"]
}
```

`checkStyleApplicability(style, dataset)` reports explicit mismatches
(kind, bands, value-range overlap, modality); `applyStyleSpecToLayer`
refuses a multiband assignment beyond the real band count. A semantically
wrong renderer is never silently substituted. The preflight maps these to
`MAP_STYLE_DATA_MISMATCH`.

### Token alias chains

`token:` references resolve transitively (`token:colors.accent` →
`token:colors.base` → `#2c7fb8`, up to `kMaxTokenHops` = 8 hops). Cycles
and over-deep chains are reported and the raw reference survives verbatim —
no wrong value is invented. Token sets may legally declare references in
`colors`/`palettes`; `resolveTokenSet` materializes chains once (problems
surface in `resolved.token_problems`).
