# Cartographic Preflight Rule Catalog (Design System 4.0)

`preflightMapSpec` evaluates a MapSpec's *resolved* composition and returns
a `map_quality_report` envelope: issues with `code`, `severity`, `message`,
`repairable`, `item_id`, and (for repairable findings) a `suggested_action`
with `action` — plus `quality_score` (0–100 = 100 − 20·errors − 8·warnings),
`passed` (no errors **and** no repairable findings), and issue counts.

The machine-readable catalog is exposed by `preflightRuleCatalog()` and the
`cartography:list_rules` tool; the table below is the human companion.
Text-overflow rules use a platform-independent width estimator (CJK glyphs
one em, others 0.55 em, spaces 0.35 em; 1 pt = 0.3528 mm; single line, 5%
tolerance) — deliberately not local font metrics.

## Bounded-resource contract

Documents are capped at 2000 items (`MAPSPEC_INVALID` beyond that) and
issue lists at 500 entries (truncation reported as
`MAPSPEC_ISSUES_TRUNCATED`) — preflight/repair are synchronous agent tools
and must stay bounded on hostile input.

## Catalog

| code | severity | repairable | repair behavior |
|------|----------|------------|-----------------|
| `MAPSPEC_INVALID` | error | no | short-circuits the report with score 0 |
| `MAP_MISSING_MAP` | error | no | a map-less spec cannot pass |
| `MAP_EMPTY_MAP` | warning | no | frame has neither layers nor extent (renders blank) |
| `MAP_MISSING_TITLE` | warning | yes | adds a `title.main` draft |
| `MAP_MISSING_LEGEND` | warning | yes | adds a `legend.primary` draft linked to the first frame |
| `MAP_MISSING_SCALE_BAR` | warning | yes | adds a `scalebar.primary` draft |
| `MAP_MISSING_NORTH_ARROW` | warning | yes | adds a `north_arrow.primary` draft |
| `MAP_MISSING_SOURCE_NOTE` | warning | yes | adds a `source.primary` draft |
| `MAP_INVALID_RECT` | error | no | non-positive rect size is a content error |
| `MAP_OFF_PAGE` | error | yes | clamps the rect into the page |
| `MAP_MARGIN_VIOLATION` | warning | yes | moves/clamps the item into the `page.margin_mm` box (furniture only; maps may bleed) |
| `MAP_TINY_FONT` | warning | yes | raises the font to 8 pt |
| `MAP_TITLE_OVERFLOW` | warning | yes | widens the rect to the estimated width; if the page cannot fit it, shrinks the font (floor 12 pt) |
| `MAP_SOURCE_NOTE_CLIPPING` | warning | yes | same strategy (font floor 8 pt) |
| `MAP_TEXT_OVERFLOW` | warning | yes | same strategy for labels/annotations |
| `MAP_LEGEND_DENSITY` | warning | yes | grows the legend height for the declared `max_entries`; when page-bound, spreads entries over `columns` (capped at 6 — beyond that the finding stays reported) |
| `MAP_DUPLICATE_FURNITURE` | warning | yes | removes **byte-identical** duplicates only; distinct content with a repeated role is kept and reported |
| `MAP_INVALID_BINDING` | warning | no | chart binding does not resolve (inline data required or layer reference missing) — fabricating data is not a repair |
| `MAP_UNBALANCED_FRAMES` | warning | yes | matches deviant frame heights (>15%) to the first frame |
| `MAP_INSET_PLACEMENT` | warning | yes | pins a frame-less locator inset into the first frame's bottom-right corner (4 mm gap) |
| `MAP_UNKNOWN_COMPONENT` | warning | yes | strips a `source_component` reference that resolved to nothing |
| `MAP_CONSTRAINT_UNSATISFIABLE` | warning | no | composition-solver leftovers (unknown items, unsolvable directions) |
| `MAP_OVERLAP` | warning | yes | relocates through seven fixed anchor slots, first collision-free candidate wins |
| `MAP_LOCATOR_MISMATCH` | warning | no | locator inset extent diverges from the referenced frame (>100×); indicator would be unreadable at render scale (Platform 5.0) |
| `MAP_ATLAS_INCOMPLETE` | error / warning | no | atlas enabled without a coverage layer (error), or `sort_order` without a sort key |
| `MAP_CONDITIONAL_CONTEXT_MISSING` | warning | no | `visible_if`/`content_if`/`page_if` declared but no `condition_context` stamped — content is kept, nothing hidden |
| `MAP_CHART_OVERFLOW` | warning | yes | table-family rows cannot fit the chart rect; repair grows the rect downward inside the page margin (`grow_chart`) |
| `MAP_PAGE_BALANCE` | warning | no | a declared page carries no items (blank export sheet) |
| `MAP_MISSING_CRS_NOTE` | warning | no | report/publication source notes do not state the CRS (CRS / EPSG / 坐标系统) |
| `MAPSPEC_ISSUES_TRUNCATED` | warning | no | issue lists cap at 500 entries; fix the reported findings and re-run |
| `LAYOUT_*` | warning | no | findings merged from the compiled-layout preflight |

## Repair loop contract

- `repairMapSpec(spec, report)` runs the **composition solver first**, then
  applies one deterministic pass per repairable issue.
- The `cartography:repair` tool loops preflight→repair up to
  `max_iterations` (clamped 1–10, default 3) and stops early when no repair
  applies — remaining non-repairable issues stay listed in the report.
- Repairs never delete meaningful content. The only removals are
  byte-identical duplicates and dangling component references that resolved
  to nothing.
- Determinism: the same broken spec + the same report produce a
  byte-identical repaired spec (pinned by tests).

## Non-repairable ≠ failure

Non-repairable warnings (empty frame, invalid binding, solver leftovers)
are advisory: they keep `passed` true when nothing repairable remains, so
the agent decides — the loop never thrashes on findings it cannot fix.

Platform 5.0: `MAP_OVERLAP` compares only items on the same page; multi-page documents are no longer flagged for cross-page geometry.

## Platform 6.0 additions

| Code | Severity | Meaning |
|---|---|---|
| `MAP_STYLE_REF_UNKNOWN` | warning | item `style_ref` does not resolve in the style registry |
| `MAP_STYLE_DATA_MISMATCH` | warning | referenced style contradicts the item binding (kind / modality / band count / value domain) |
| `MAP_UNCERTAINTY_NOTE_MISSING` | warning | uncertainty/probability-styled document carries no uncertainty note |

The composition solver behind `MAP_CONSTRAINT_UNSATISFIABLE` is now a
bounded constraint graph solver: relaxation runs at most 24 passes, converges
to a fixpoint that is independent of constraint declaration order for
consistent systems, and reports cycles / non-convergence with the
constraint ids involved instead of moving items silently. The composition
report adds `constraints_total`, `passes` and `converged`.

## Platform 7.0 additions

| Code | Severity | Repairable | Meaning |
|---|---|---|---|
| `MAP_INVISIBLE_LAYER` | warning | no | declarative layer has `visible: false` or `opacity <= 0` — it will not render |
| `MAP_LAYER_UNREFERENCED` | warning | yes | layer declared in `layers[]` is referenced by no map frame or inset; repair attaches it to the main frame (dedup-safe) |
| `MAP_LEGEND_MISMATCH` | warning | no | explicit legend `classes[]` labels missing from the referenced style's class/category labels (first missing label named) |
| `MAP_TEXT_WRAP_OVERFLOW` | warning | yes | wrap-aware text layout (word wrap, CJK kinsoku, ≤64-line budget) does not fit the item rect; the single-line estimator rules above only see the widest hard line |

The wrap-aware rule runs through the typography engine
(`cartography/typography.h`): deterministic per-codepoint width classes
(identical to the estimator), greedy word wrap, CJK kinsoku (no line starts
with closing punctuation, none ends on opening punctuation), declared
truncation policies (`none | ellipsis | shrink_to_fit | overflow_report`)
and a structured `TextFitReport` used for overflow diagnostics.

The composition solver behind `MAP_CONSTRAINT_UNSATISFIABLE` is now the
Platform 7.0 explainable solver: constraints may declare
`hardness`/`priority`/`weight` (MapSpec v4), the fixpoint is chosen by a
canonical policy (declaration-order independent even for over-determined
systems), soft conflicts are reverted with the winning constraint named,
and every unsatisfied hard constraint carries a bounded unsat core. See
`docs/cartography/migration-mapspec-v4.md`.

## Platform 8.0 additions

| Code | Severity | Repairable | Meaning |
|---|---|---|---|
| `MAP_NODATA_LEGEND` | warning | yes | the referenced style declares `raster.nodata` but the legend carries no nodata mention — the NoData class is invisible to the reader; repair stamps `legend.nodata` from the style |

The page-aware solver evidence is also new: `keep_with` and
`avoid_overlap` respect the follower's declared page height. A pin that
would push the companion past its own page bottom is refused with a
`page_overflow` reason in `unsatisfied`, `violated` and the decisions
ledger, instead of silently producing off-page geometry that only
`MAP_OFF_PAGE` would find later without provenance. A permanent refusal
like this cannot appear in the bounded unsat cores: no subset removal of
other constraints changes the page bound that caused it.
