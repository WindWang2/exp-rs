# Migrating MapSpec v1 → v2 (Design System 4.0)

MapSpec `spec_version` 2 (ADR 0131) is a **strict superset** of v1: every
new field is optional, so existing v1 documents are valid v2 documents.

## What changed

| area | v1 | v2 |
|------|----|----|
| version ceiling | `spec_version ≤ 1` | `spec_version ≤ 2`; newer documents rejected with an explicit problem |
| style | implicit (hard-coded 18 pt titles, 7 pt notes) | `style` block: token set + medium + overrides (ADR 0130) |
| geometry | raw `rect_mm` only | optional `anchor`, `min_size_mm`, `max_size_mm` resolved by the composition solver |
| ordering | compile order only | optional `z_index` per item |
| pages | single page | `pages[]` (≤ 10 extra) + per-item `page` index |
| map series | — | `page.atlas` hook (`enabled`, `coverage_layer`, `filename_expression`) |
| bindings | — | `slots[]` role→item bindings, `binding` metadata, `MAP_INVALID_BINDING` rule |
| constraints | free-form | typed kinds (`align`, `match_width`, `match_height`, `stack`, `distribute`) validated and solved |
| component refs | carried but ignored | resolved at compile time; unknown refs reported (`MAP_UNKNOWN_COMPONENT`) and stripped by repair |
| extract | font-heuristic label classification | stamped `semantic_role` custom property round-trips faithfully |

## What you must do

**Nothing, usually.** `upgradeMapSpec` chains v0→v1→v2 in memory and is
idempotent; `MapSpecCompiler::compile` accepts any document with
`spec_version ≤ 2`. Saved documents upgrade to `spec_version: 2` the next
time they pass through the pipeline.

## Behavior notes

- v1 docs without a `style` block compile with the default
  `scientific-light` token set — title/label/note fonts now follow the
  token hierarchy (title 20 pt by default, previously hard-coded 18 pt).
  Pass explicit `font.size_pt` to pin the old appearance.
- `applyComponentDefaults` now merges `source_component` defaults into
  items at compile time; items that already specified all fields are
  unaffected (explicit fields always win).
- Charts without `rect_mm` get a token-derived canvas and a deterministic
  bottom-left placement instead of falling to the layout default position.
- New preflight rules (overflow, margins, density, duplicates, insets,
  bindings, balance) can flag previously-silent documents; run
  `cartography:preflight` / `cartography:repair` to converge them.

## Deprecated / removed

Nothing removed. The v0 `items: [{kind, …}]` draft format still migrates.
The `required_slots` template key remains accepted beside `slots`.
