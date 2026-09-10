# MapSpec Reference (versioned; current: 3.0)

A MapSpec is a versioned JSON document describing a map product as semantic
collections. It compiles to a `QgsPrintLayout` through `MapSpecCompiler`;
QGIS Layout stays the authoritative renderer (ADR 0127, 0131).

> **Platform 5.0 (spec_version 3)** adds bounded conditional visibility
> (`visible_if`/`content_if`/`page_if`), relative placement constraints,
> inset locator extent indicators, the full atlas surface, page roles and
> item `style_ref`. v3 is a strict superset of v2 — everything in this
> document still holds. See `migration-mapspec-v3.md` for the additions and
> the v2→v3 migration path; `spec_version` values ≤ 3 validate.

## Envelope

```jsonc
{
  "generated_by": "exp-rs",       // envelope (contracts)
  "generated_at": "…",            // envelope timestamp
  "kind": "map_spec",             // required
  "spec_version": 3,              // required integer, ≤ 3
  "layout_name": "my-map",        // required; join key for the layout
  "template": "land-cover-a4l",   // optional provenance
  "page": {                       // required, positive sizes
    "width_mm": 297, "height_mm": 210,
    "margin_mm": 12,              // optional; enables the margin rule
    "atlas": {                    // optional map-series hook
      "enabled": true,
      "coverage_layer": "layer-ref",
      "filename_expression": "'sheet_' || $id"
    }
  },
  "style": { "token_set": "scientific-light", "medium": "print",
             "overrides": {} },  // optional (ADR 0130)
  "pages": [ { "width_mm": 210, "height_mm": 297 } ], // optional, ≤10 extra pages
  "slots": [ { "role": "title.main", "item": "title-1" } ], // optional bindings
  "…collections": []
}
```

## Collections

`map_frames, layers, symbols, legends, north_arrows, scale_bars, titles,
labels, charts, colorbars, inset_maps, grids, annotations, source_notes,
constraints` — arrays of items with a unique string `id` (auto-assigned
`<prefix>-<n>` by `appendMapSpecItem`).

### Item fields (all collections)

| field | type | notes |
|-------|------|-------|
| `id` | string | unique across the document |
| `rect_mm` | [x, y, w, h] | page millimeters; optional where the compiler applies defaults |
| `semantic_role` | string | `title.main`, `legend.change`, … (duplicate furniture roles flagged) |
| `map_ref` | string | must resolve to a map frame where allowed |
| `source_component` | string or {id, variant} | component defaults merged under the item |
| `anchor` | {edge, margin_mm} | solver resolves to rect origin |
| `min_size_mm` / `max_size_mm` | [w, h] | solver clamps |
| `z_index` | integer | item stacking (QgsLayoutItem z-value) |
| `page` | integer | 0-based target page (≤ declared extra pages) |
| `binding` | object | data-binding metadata |

### Collection specifics

- `titles` require `text`; `source_notes` require `text`; `charts` require
  a `chart` object (`kind`, `binding.mode` = inline | vector_expression;
  inline data ≤256 points; `matrix` charts need `binding.matrix`).
- `legends` accept `title`, `columns` (>1 takes the legend out of QGIS
  auto-update), `max_entries` (density rule input).
- `scale_bars` accept `style` (QGIS renderer name), `units` (label),
  `units_per_segment`.
- `north_arrows` accept `svg` (resource or path) and `north_mode`
  (true | grid | magnetic).
- `colorbars` accept `ramp` (token palette name or built-in
  viridis/sequential/heat/blue), `colors` (explicit hex stops, win over
  ramps), `min`, `max`, `font_pt`, `text_color`.
- `grids` accept `interval` (CRS units) and `map_ref`.
- `inset_maps` compile as secondary map frames; explicit `extent`, else
  inherited from `map_ref`/first frame (locator outline drawn by shared
  layers).

## Constraints (solver-enforced)

```jsonc
{ "id": "c1", "kind": "align",       "items": ["a", "b"], "edge": "top|bottom|left|right" }
{ "id": "c2", "kind": "match_width", "items": ["a", "b"] }
{ "id": "c3", "kind": "match_height","items": ["a", "b"] }
{ "id": "c4", "kind": "stack",       "items": ["a", "b"], "direction": "below|above|left_of|right_of", "gap_mm": 4 }
{ "id": "c5", "kind": "distribute",  "items": ["a", "b", "c"], "direction": "horizontal|vertical" }
```

Solved once each in declared order (deterministic, bounded). Unsatisfiable
constraints are reported (`MAP_CONSTRAINT_UNSATISFIABLE`), never silently
ignored.

## Validation & migration

- `validateMapSpec` — envelope, version ceiling, page geometry, per-item
  required fields, id uniqueness, rect well-formedness, reference integrity
  (map_ref, slots, constraints, component refs), v2 field shapes.
- `upgradeMapSpec` — chains v0→v1→v2 in memory; idempotent; documents
  newer than the supported version are rejected with an explicit problem.
- `applyMapSpecPatch(es)` — `add` / `update` (shallow merge, immutable id) /
  `remove`; agents patch instead of resending documents.

## Compilation pipeline

```
validate → create/replace layout (page sizes, extra pages, atlas)
→ resolve tokens → apply component defaults → composition solver
→ items (maps, grids, text furniture, legends, scale bars, north arrows,
  insets, charts, colorbars)
→ post-pass (page moves, z_index, semantic_role stamping)
```

`extract(layout)` mirrors a layout back to MapSpec; stamped semantic roles
round-trip faithfully, non-mappable QGIS items surface as annotations with
a `qgis_type` marker (documented divergence).

## Platform 6.0 notes

- **Children**: items may carry a bounded `children[]` array (role-qualified
  blocks materialized from composite components; depth 1, ≤ 16, unique
  roles) — validated with the item grammar.
- **Conditions**: `resolveMapSpecConditions` accepts the runtime context as
  an argument; an embedded `condition_context` is optional and merged *under*
  the external context (external keys win) — #802. Both operands of
  `and`/`or` are always evaluated so evaluation errors can never hide behind
  short-circuit, and bare-literal conditions are rejected at validation
  time — #804.
- **Solver**: constraints resolve through a bounded constraint-graph
  pipeline (normalize → dependency graph + cycle detection → propagation →
  ≤ 24 relaxation passes → collision handling → scoring → convergence
  report); consistent systems converge to declaration-order-independent
  geometry — #805, #781.

## Platform 7.0 notes

- **spec_version 4** (strict superset of v3): constraint items may declare
  `hardness` (`"hard"` default | `"soft"`), `priority` (integer 0..100,
  default 50) and `weight` (0..1000, default 1, soft-only). See
  `docs/cartography/migration-mapspec-v4.md` for the full surface.
- **Explainable solver**: canonical constraint ordering (hard before soft,
  priority desc, weight desc, declaration index) picks the fixpoint —
  declaration permutations no longer change over-determined outcomes; soft
  constraints apply after the hard fixpoint and yield (reverted + reported)
  to higher-ranked constraints; the composition report adds
  `soft_total/soft_satisfied`, `satisfied_weight/violated_weight`,
  `decisions[]`, `violated[]`, `unsat_cores[]` and `fixpoint_policy`.
- **Unsat cores**: every unsatisfied hard constraint reports the minimal
  conflicting subset found within the bounded search radius (candidates ≤ 8,
  subset ≤ 3, ≤ 32 simulations; truncation marked `bounded: true`).
- **Typography**: text furniture is measurable through the deterministic
  engine in `cartography/typography.h` (UTF-8 codepoint width classes, word
  wrap, CJK kinsoku, `none|ellipsis|shrink_to_fit|overflow_report`
  truncation policies, structured fit reports). Preflight consumes it via
  `MAP_TEXT_WRAP_OVERFLOW`.
