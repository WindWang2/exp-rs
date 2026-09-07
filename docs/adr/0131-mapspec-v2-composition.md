# ADR 0131: MapSpec 2.0 — Compositional Constraints, Composition Solver & Visual Regression

- Status: Accepted (Design System 4.0, Milestones D/E/F)
- Context: MapSpec v1 items carried raw `rect_mm` only; every normal
  composition forced an agent to hand-compute millimeter coordinates.
  Preflight covered presence/overlap/font rules only, and no test ever
  rendered a layout — JSON assertions could not see drawing defects.

## Decision

1. **MapSpec `spec_version` 2, additive and strict**: v2 is a strict
   superset of v1 (every new field optional; v1 docs validate as v2).
   New surface — only what the compiler and validator genuinely enforce:
   - `style` block (token_set/medium/overrides, ADR 0130);
   - item `anchor {edge, margin_mm}`, `min_size_mm`/`max_size_mm`,
     `z_index`, `page` index, `binding` metadata;
   - `slots[]` (role→item bindings, uniqueness validated);
   - typed `constraints`: `align`, `match_width`, `match_height`, `stack`,
     `distribute` — kinds are validated enums; edge/direction values are
     shape-checked at validation and enum-enforced by the solver
     (unsolvable values surface as `MAP_CONSTRAINT_UNSATISFIABLE`);
   - `pages[]` (≤10, positive sizes) with per-item page index;
   - `page.atlas` hook (enabled/coverage_layer/filename_expression →
     `QgsLayoutAtlas`);
   - `page.margin_mm` (drives the margin rule when declared).
2. **Composition solver** (`resolveComposition`): deterministic pre-compile
   pass — anchors → concrete rects, size clamps, then constraints solved
   once each in declared order (no fixpoint iteration, no backtracking).
   Mutates geometry only; unsatisfiable outcomes are reported
   (`CompositionResult.unsatisfied`, mirrored as
   `MAP_CONSTRAINT_UNSATISFIABLE`), never silently "fixed" by moving other
   items. Same input yields a byte-identical document.
3. **Multi-page + z-order compile**: additional declared pages are created;
   items with `page > 0` are moved via `QgsLayoutItem::attemptMove(..., page)`;
   `z_index` sets `QgsLayoutItem` z-values. Compilation stamps
   `semantic_role` as an item custom property so `extract` round-trips
   faithfully (the v1 bold-font heuristic misclassified labels as titles).
4. **Repair loop owns the solver**: `repairMapSpec` runs
   `resolveComposition` before applying fixes, so anchor/constraint-bearing
   specs converge in the same bounded loop as plain rects. Repairs are
   deterministic, never delete meaningful content — the only removals are
   byte-identical duplicates and dangling `source_component` references
   that resolved to nothing — and residual issues stay reported.
5. **Preflight rule catalog** (machine-readable via
   `preflightRuleCatalog()` / `cartography:list_rules`): new rules for text
   overflow (platform-independent width estimator — CJK one em, others
   0.55 em — not font metrics), declared-margin violations, legend density
   (declared `max_entries` × token line height), duplicate furniture,
   invalid chart bindings, unbalanced multi-map frames, inset placement,
   unknown component references, unsatisfiable constraints.
6. **Visual regression methodology** (Milestone F): the harness renders the
   benchmark set (classification, change, time series, SAR, scientific
   publication, multi-panel, dense legend, CJK title) through
   `QgsLayoutExporter` and asserts (a) byte-identical PNGs for repeated
   renders (determinism), (b) geometry contracts separately from pixels,
   and (c) optional out-of-tree golden references
   (`SICNU_CARTOGRAPHY_GOLDEN_DIR`, downscaled 25%, mean-abs-diff < 12) so
   no giant binaries enter the repository and font anti-aliasing
   differences cannot flake CI.

## Consequences

- `spec_version` 0/1 documents upgrade in memory through the chained
  `upgradeMapSpec` (idempotent); no on-disk migration is required.
- Agents compose with roles and anchors; raw coordinates remain available
  but are no longer required for normal compositions.
- Deferred (documented, not implemented): full atlas corner cases
  (sorting/filtering beyond the hook), rule-based conditional rendering at
  compile time (conditionals are template-instantiation-time), dark-print
  guarantee, per-glyph text measurement.
