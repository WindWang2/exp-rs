# Template Authoring Guide (Cartography Design System 4.0)

Templates are task-oriented map compositions: semantic slots, component
recommendations and a style reference. `instantiateTemplate` turns a
template into a concrete MapSpec draft that agents patch; templates never
require an agent to compute raw millimeter coordinates for normal
composition. They live in `data/cartography/templates/*.json`.

## Descriptor

```jsonc
{
  "id": "land-cover-a4l",
  "version": 1,
  "description": "Land-cover product map with locator inset …",
  "page": { "width_mm": 297, "height_mm": 210 },
  "style": { "token_set": "scientific-light", "medium": "print" },
  "slots": [
    { "role": "map.main", "accepts": "map_frames", "rect_mm": [12, 30, 191, 130] },
    { "role": "legend.primary", "accepts": "legends", "rect_mm": [215, 30, 70, 82],
      "content": { "title": "图例", "columns": 1 },
      "component": { "id": "legend/categorical", "variant": "single-column" } }
  ],
  "recommended_components": ["north-arrow/minimal", "grid/graticule"],
  "suitable_tasks": ["land-cover", "land-use"],
  "product_type": "land_cover"
}
```

- `slots` supersedes the v1 `required_slots` (still accepted). Every slot
  has a unique `role` and an `accepts` collection; `rect_mm` positions it;
  `content` is an item-shaped draft deep-merged into the instantiated item;
  `component` supplies component defaults the same way.
- `recommended_components` entries are component ids or `{id, variant}`;
  the registry places them (north arrows in the top-right margin strip,
  grids bound to the first map frame, …) and maps categories to collections
  via `collectionForCategory`.
- `style` travels into the draft, so the token set applies at compile time.

## Inheritance (`extends`)

Page families are variants of one base: `classification-a4p` extends
`classification-a4l` and re-declares `page` + `slots` at the new size.
Resolution (at load time, cycle-safe, `loadProblems()`):

- child slot with the same `role` replaces the parent's slot entirely;
  other parent slots are inherited;
- `recommended_components` and `suitable_tasks` concatenate (parent first,
  duplicates dropped);
- `style` deep-merges; other fields (description, page, product_type) are
  replaced by the child;
- the resolved descriptor records `extends` for the gallery.

## Layout rules (generator-enforced, drift-checked)

- stay inside the page margin (default 12 mm);
- map frames must not overlap each other; furniture must not overlap
  furniture; furniture may overlay map frames (north arrows on the map are
  classic cartography — the preflight overlap rule treats map frames as
  overlays, not obstacles);
- slot roles follow `map.main`, `title.main`, `legend.primary`,
  `scalebar.primary`, `north_arrow.primary`, `source.primary`, and
  suffixed variants (`map.t1`, `map.before`, `legend.change`).
- sample data in chart `content` is a placeholder the agent replaces — it
  exists so a draft compiles out of the box.

## Quality gates (all drift-tested)

1. instantiate → `validateMapSpec` empty;
2. preflight → repair loop (≤6 passes) → `passed: true`;
3. compiled through `MapSpecCompiler` into a `QgsPrintLayout`;
4. catalog index (`data/cartography/index.json`) and gallery doc list the
   template.

## Agent workflow (acceptance intents)

```
discover   cartography:list_templates {task: "land-cover"}
bind       cartography:instantiate_template {template, params: {title, source_note, layers, extent,
                                            slots: {<role>: {…overrides…}}}}
compose    cartography:compose          {mapspec}   → compiled + quality + composition
preflight  cartography:preflight        {mapspec}
repair     cartography:repair           {mapspec, max_iterations?}  → passed
export     layout:export                (QGIS Layout remains the renderer)
```

## Agent surface

`cartography:list_templates` (paged, task filter),
`cartography:instantiate_template`, `cartography:catalog_index`.

## Platform 6.0 — facets, variants and explainable search

Templates declare orthogonal **facets** instead of proliferating
near-duplicate files:

```jsonc
"facets": { "tasks": ["classification", "vegetation"],
            "medium": "a4",            // screen|a4|a3|a0|report|atlas
            "purpose": "analysis" },   // exploration|analysis|operational|scientific|presentation
"variants": [ { "id": "a3-landscape", "page": { "width_mm": 420, "height_mm": 297 } } ]
```

- Facet vocabularies are closed; `validateTemplateFacets` rejects typos so
  search cannot silently miss.
- `variants` parameterize page geometry inside ONE descriptor (controlled
  inheritance stays `extends`).
- `searchTemplates(templates, query)` (and `TemplateRegistry::search`)
  filters by `task`/`medium`/`purpose`/`keyword`, returns compact summaries
  plus `match: {score, reasons[]}` — facet-complete documents outrank legacy
  ones; results are deterministic (score desc, id asc).
- `cartography:list_templates` accepts `medium`/`purpose`/`keyword` and
  routes through the same search, keeping responses inside the token budget.
