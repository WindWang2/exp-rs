# Atlas / Map Series Guide (Platform 5.0)

The atlas turns one MapSpec into a map series: QGIS iterates the features of
a coverage layer and renders one page per feature. Platform 5.0 promotes the
4.0 hook into a usable, validated feature compiled to the QGIS-native
`QgsLayoutAtlas`.

## Configuration (page.atlas)

```json
{
  "page": {
    "width_mm": 297, "height_mm": 210,
    "atlas": {
      "enabled": true,
      "coverage_layer": "regions",
      "filter": "\"status\" = 'active'",
      "sort_by": "name",              // or "sort_expression"
      "sort_order": "asc",            // asc | desc
      "filename_expression": "'map-' || \"name\"",
      "margin_fraction": 0.05,
      "feature_variables": ["name", "area_km2"]
    }
  }
}
```

| key | compile target | validation |
|---|---|---|
| `enabled` | `QgsLayoutAtlas::setEnabled` | boolean |
| `coverage_layer` | `setCoverageLayer` (workspace ref resolution) | string; **required** when enabled (`MAP_ATLAS_INCOMPLETE` error) |
| `filter` | `setFilterExpression` + `setFilterFeatures(true)` | string (QGIS validates syntax at atlas preparation) |
| `sort_by` / `sort_expression` | `setSortExpression` + `setSortFeatures(true)` | string |
| `sort_order` | `setSortAscending` | `asc` \| `desc`; needs a sort key |
| `filename_expression` | `setFilenameExpression` | string |
| `margin_fraction` | map item `setAtlasMargin` | number in [0, 1) |
| `feature_variables` | knowledge (documented fields) | array ≤ 32 |

## Dynamic text

QGIS evaluates inline expressions in label text at render time:

```json
{ "role": "title.main", "accepts": "titles",
  "content": { "text": "附录 — [% \"name\" %]" } }
```

Page numbering: `[% @atlas_feature_number %] / [% @atlas_total_features %]`.
`feature_variables` documents which feature fields the template expects;
`solution:instantiate` passes the coverage layer through, and preflight
checks the atlas block's completeness.

## End-to-end (agent flow)

1. `solution:search {task: "flood"}` → `solution.flood.atlas-series`
2. `solution:instantiate` with the coverage layer bound → MapSpec draft
   (template `atlas-series-a4l` / `report-atlas-appendix-a4l` already carry
   the atlas block).
3. `cartography:compose` — the compiler attaches coverage/filter/sort/margin.
4. `cartography:preflight` — `MAP_ATLAS_INCOMPLETE` catches the missing
   coverage layer before export.
5. `layout:export` with the atlas enabled exports per-feature pages named by
   `filename_expression`.

## Testing

`test_platform5.cpp` validates the atlas surface (v3 keys, sort order rule,
margin fraction range) and the report-atlas template instantiates with the
appendix blocks. Visual atlas coverage (multi-feature render) runs in the
opt-in golden path — see `visual-regression.md` for the out-of-tree baseline
strategy. The 4.0 limitation entry ("no atlas beyond the hook") is lifted;
feature-driven *symbology overrides* remain QGIS-native configuration through
`layout:*` / `symbology:*` tools.
