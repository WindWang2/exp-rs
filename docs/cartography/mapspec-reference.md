# MapSpec Reference (versioned; current: 5.0)

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
  "spec_version": 5,              // required integer, ≤ 5
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
  "…collections": [],
  "output": {                     // optional (v5): delivery declaration
    "formats": ["png", "pdf"],    //   closed vocabulary; ≤ 8 entries
    "dpi": 300,                   //   72..1200
    "dir": "exports"              //   non-empty when declared
  }
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


## Platform 8.0 notes

- **spec_version 5** (strict superset of v4): the envelope may declare an
  `output` block (formats `png`/`pdf`, dpi 72..1200, optional dir). It is
  validated and surfaced through `cartography:compose` /
  `confirmMapOutput`, but compilation never auto-exports — delivery stays an
  explicit governed action. `upgradeMapSpec` re-stamps v≤4 documents to 5.
- **Typed bindings**: per-item `binding` objects are shape-validated
  (non-empty string `mode`; string `layer`/`field`/`expression`
  fields; object `params`; `data` bounded at 256 entries; square
  `matrix {labels, rows}` bounded at 24×24) without breaking shipped
  templates.
- **Locator connectors**: `inset_maps[].locator.connector` (object; optional
  `style: solid|dash`, `stroke_mm ≤ 5`, `color`) compiles to a QGIS-native
  polyline from the inset frame edge to the referenced frame's projected
  extent anchor (`<id>-locator-connector`).
- **Declared typography**: text items may declare `font.break_policy`
  (`none | halfwidth` — line-final fullwidth closing punctuation
  compression) and `font.line_height` (0 < h ≤ 3, leading override used by
  the wrap-aware overflow rule).
- **NoData legend**: `legends[].nodata = {label}` compiles as a QGIS-backed
  swatch composite inside the declared legend rect; the
  `MAP_NODATA_LEGEND` preflight rule keeps declarations honest (see
  `docs/cartography/preflight-rules.md`).
- **Page-aware solver evidence**: `keep_with`/`avoid_overlap` refuse pins
  that would push a companion past its own page bottom with a
  `page_overflow` reason (carried in unsatisfied, violated and the decisions
  ledger) instead of silently writing off-page geometry.

## Platform 9.0 additions

### New constraint kind: `page_break`

```jsonc
{ "id": "pb1", "kind": "page_break", "items": ["table-1"] }
```

Moves the single declared item to the next declared page (`page: n` →
`n + 1`). Validation requires the target page to exist in `pages[]`; the
solver applies it in the first relaxation pass and stamps
`page_break_applied_by` so re-solving a resolved document never re-breaks
it. It reports through the decisions ledger and violations; like every
permanent refusal it never enters the bounded unsat cores. This is the one
documented solver write outside `rect_mm` — page index plus the provenance
stamp.

### Text-driven sizing (`fit_content.text_ref`)

```jsonc
{ "id": "fit1", "kind": "fit_content", "items": ["title-2"], "text_ref": "label-1" }
```

`fit_content` may derive its content box from a referenced text item at
solve time under the deterministic typography model (width = widest wrap
line, height = lines × pt × leading). The wrap width is the item's declared
rect width, else its `max_size_mm` width, else single-line. Exactly one of
`content_mm` / `text_ref` may be declared; `text_ref` must resolve.

### Master furniture (`pages[].furniture`)

```jsonc
"pages": [ { "width_mm": 297, "height_mm": 210, "role": "map",
             "furniture": ["title-1", "source-1"] } ]
```

Each referenced item is materialized as a clone (`<id>-p<page>`,
provenance `master_of: <id>`, declared `page`) before validation, so clones
flow through the same compile/solve/page-placement paths as hand-declared
furniture. Clones keep the master's rect (same relative position per page).
References must resolve and must not repeat within a page (≤32 entries).

### Atlas-driven text (`expression`)

Titles and labels may declare `expression` (a QGIS expression string). It
compiles to native `[% … %]` label markup evaluated at render time against
the layout expression context (atlas feature included). An unparseable
expression is a compile failure, never silently static text; `expression`
beats `text` when both are declared.

### Cross-page references (`continuation`)

```jsonc
{ "id": "table-1", "page": 1, "continuation": { "label": "continued on" } }
```

An item past page 0 with a `continuation` block compiles a small caption
(`<id>-continuation`) under its rect naming the DISPLAY page number
(1-based). Resolved after the solver runs, so solver-applied page breaks
participate.

### Declared chart overlays (`overlay_on`)

A chart or colorbar that intentionally covers a map frame declares it:

```jsonc
{ "id": "chart-1", "overlay_on": "map-1", "rect_mm": [215, 130, 70, 44], … }
```

`overlay_on` accepts one frame id or an array of frame ids (validated to
resolve). Coverage declared this way is intentional and `MAP_CHART_OVER_MAP`
stays silent; undeclared coverage is flagged, and repair prefers moving the
chart, stamping `overlay_on` only when no free slot exists.

### Solver evidence surfaces

`cartography:compose` / `resolveComposition` now also report
`oscillations` (the constraints still writing when the pass budget was
exhausted — the contradictory cycle behind a non-converged layout; the
layout itself rolls back to its pre-solve snapshot) and `trace` (the
bounded per-pass record of which constraints wrote). `fit_content` and
`page_break` behave under the same bounded-pass budgets as before.

### Governed export and explanation

`cartography:export` (atomic, sha256-digested, page-selectable for png) and
`cartography:explain` (bounded per-item solver + preflight evidence) join
the typed tool surface; see `cartography_tools` descriptions for contracts.
