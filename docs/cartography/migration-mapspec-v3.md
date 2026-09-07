# Migrating MapSpec v2 → v3 (Platform 5.0)

MapSpec **v3 is a strict superset of v2**: every new field is optional, v2
documents validate unchanged, and `upgradeMapSpec` migrates v0/v1/v2 inputs to
`spec_version: 3` in place (idempotent). No field was renamed or removed.

## What v3 adds

### 1. Bounded conditional visibility

Items: `visible_if`, `content_if`. Page entries: `page_if`. The grammar is a
bounded expression language — comparisons (`== != > >= < <=`), `and`/`or`,
`has(path)`, booleans, numbers, quoted strings, dotted paths. Length ≤ 256
chars, ≤ 64 tokens, depth ≤ 8. There is no function-call escape hatch; the
conditions are *data*, validated by `validateMapSpec` and evaluated by
`mapspec::evaluateCondition` against a caller-materialized context.

Semantics (see `resolveMapSpecConditions`):

| field | outcome when false |
|---|---|
| `visible_if` | item is removed before compile |
| `content_if` | the item's `content` member is stripped; an item left without any intrinsic content (text/chart/colors) is removed |
| `page_if` | the page is removed; its items are removed; remaining page indices remap compactly |

Safety contract: an unevaluable condition (unknown context path, type error)
**keeps its content** and is reported in the evaluation ledger — nothing
scientific is ever silently hidden. Context is stamped as
`spec.condition_context` by the caller (e.g. `solution:instantiate`) and
consumed at compile; `cartography:preflight` emits
`MAP_CONDITIONAL_CONTEXT_MISSING` when conditions exist without a context.

### 2. Relative placement constraints

New solver-enforced constraint kinds (single deterministic pass each, in
declaration order; unsatisfiable outcomes are reported, never silently fixed):

| kind | items | effect |
|---|---|---|
| `below` / `above` / `left_of` / `right_of` | exactly 2 `[target, follower]` | follower placed relative to target (+ `gap_mm`, default token margin) |
| `inside` | exactly 2 `[container, content]` | content centered in the container, clamped inside |
| `keep_with` | exactly 2 `[anchor, companion]` | companion pinned below the anchor, horizontal position preserved |
| `avoid_overlap` | exactly 2 `[keeper, mover]` | mover pushed below the keeper only when they intersect |
| `fit_content` | exactly 1 + `content_mm` | item resized to the declared content size, clamped by min/max_size_mm |

### 3. Locator extent indicators

`inset_maps[].locator = {target, style, label, stroke_mm, label_size_pt}` —
`target` must resolve to a map frame. The compiler attaches a QGIS-native map
overview (`QgsLayoutItemMapOverview::setLinkedMap`) so the inset draws the
referenced frame's extent — a real layout primitive, not a screenshot hack.
`style`: `outline` (default inverted-shading frame), `region` (translucent
accent fill), `frame` (stroke only). `label` compiles a token-styled caption
below the inset. Preflight adds `MAP_LOCATOR_MISMATCH` (extent ratio > 100×).

### 4. Full atlas surface

`page.atlas` accepts (all optional): `filter` (QGIS expression), `sort_by` or
`sort_expression`, `sort_order` (`asc|desc`), `margin_fraction` ∈ [0,1)
(applied as the map item's atlas margin), `feature_variables` (≤ 32,
documentation of available fields), plus the v2 keys `enabled`,
`coverage_layer`, `filename_expression`. Dynamic labels/titles use QGIS
inline expressions `[% "field" %]` passed through verbatim. Preflight emits
`MAP_ATLAS_INCOMPLETE` when the atlas is enabled without a coverage layer.

### 5. Page roles & item style refs

Page entries may declare `role: cover|map|report|appendix`. Items may carry
`style_ref: "<style id>"` (resolved by the style tooling at compile/apply
time). `inset_maps` may also carry `locator` (above).

## Migration steps for existing documents

1. Nothing is mandatory — v2 documents keep working.
2. To adopt: run `upgradeMapSpec` (bumps `spec_version`), then add fields as
   needed. Absolute `rect_mm` layouts stay valid; prefer
   anchors/constraints/`visible_if` for new work so content drives geometry.
3. The catalog drift test (`cartography:catalog_index`) covers the new
   sections (`styles`, `solutions`); regenerate when assets change.
