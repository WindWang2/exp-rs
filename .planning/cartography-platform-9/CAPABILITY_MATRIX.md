# CAPABILITY MATRIX — proven against master @ `f316dfdbb4`

Per the no-duplication rule: every planned 9.0 capability is first checked
for an existing implementation, its caller, its public surface, tests, docs
and bounds. Status: **HAS** (exists, do not rebuild — at most extend),
**PARTIAL** (exists with a proven gap — extend in place), **MISSING**
(build new in owned files).

## M0/M1 — solver & expressions

| Capability | Status | Evidence |
|---|---|---|
| hard/soft/priority/weight constraints | HAS | `composition.cpp` v4 surface; validated by `validateMapSpec`; tests in test_platform7.cpp |
| anchors, min/max clamps, safe margins | HAS | `resolveAnchors`/`clampSizes` |
| align/match/stack/distribute/relative kinds/inside/keep_with/avoid_overlap/fit_content | HAS | `computeTargets` (all 13 kinds) |
| unsat core (bounded) | HAS | `searchCores`, kMaxCoreCandidates=8/subset 3/simulations 32 |
| decisions ledger + fixpoint policy | HAS | `CompositionResult.decisions`, `fixpointPolicy` |
| non-convergence rollback (oscillation #864) | HAS | `restoreRects(mPreSolveRects)` on budget exhaustion |
| oscillation *detector* (A↔B flip diagnostic distinct from budget exhaustion) | MISSING | M1: detect per-constraint satisfaction-state cycling across passes; report as `oscillation` cause with the flipping constraint ids |
| convergence trace (per-pass movement log for evidence) | MISSING | M1: bounded per-pass trace in the result JSON (`passes_used` per constraint movement counts) |
| condition AST (and/or/cmp/has/paths), bounds | HAS | `mapspec_conditions.cpp` |
| NaN semantics, has() semantics (#866/#877) | HAS | fixes in base; M0 adds named regression entries |
| mixed-kind ordering totality (bool<number? string vs bool) | PARTIAL | mixed kinds are errors except equality — semantic contract documented; M0 pins it with tests, no new language |
| deterministic evaluation | HAS | pure functions over JSON |
| fit_content incl. zero-rect fallback (#865) | HAS | leader rect fallback |
| **text-driven sizing** (fit_content from typography measurement) | MISSING | M1: `fit_content.text` mode where content_mm comes from `fitTextIntoBox` at preflight/repair time via a typed solver input, not a new renderer |
| **incremental recompute** | PARTIAL | full re-solve is O(passes×constraints) and bounded; M1 adds `resolveCompositionDelta`-style scoped re-solve entry (solve only constraints touching changed items) used by repair loop to keep passes bounded |
| numeric/string/bool ordering | HAS | `compareValues` total ordering per-kind |

## M2 — multi-page / atlas

| Capability | Status | Evidence |
|---|---|---|
| pages[] with per-page size, item page index | HAS | `mapspec.cpp` validation + compiler post-pass |
| page-aware pins (keep_with/avoid_overlap refuse page_overflow) | HAS | 8.0; `pageBottomFor` |
| atlas hook (enabled/coverage/filter/sort/margins) | HAS | `page.atlas` → QgsLayoutAtlas |
| page roles, page_if conditional pages, compact remap | HAS | v3; `resolveMapSpecConditions` |
| **master furniture per page** (repeat title/footer on every page) | MISSING | M2: `pages[].furniture`/template-level `master` with explicit per-page materialization at compile (items cloned with `master_of` provenance), deterministic ids |
| **page-dependent title/legend text** (atlas feature-driven) | MISSING | M2: declared `title.expression`/`legend.expression` compiled to QgsLayoutItemLabel text through QgsExpression evaluated against the atlas feature (QGIS-native; validated non-empty) |
| **page breaks driven by content** (overflow → next page) | PARTIAL | solver refuses page-crossing pins but never *moves* content across pages; M2 adds explicit `page_break` constraint kind + repair action that re-pages items deterministically |
| **cross-page references** (e.g. "continued on page 2" labels) | MISSING | M2: declared `continuation` block compiled to labels with resolved page numbers after layout |
| export single page vs all | PARTIAL | export is a harness action; M9 wires `pages:[n]` selection |
| no page-0 coordinate assumptions | PARTIAL | derived furniture has `placeWithParentPage`; M2 audits every compile path for per-page placement |

## M3 — component system

| Capability | Status | Evidence |
|---|---|---|
| component catalog (57 components incl. chart/table/colorbar/inset/publication/statistics/uncertainty/accuracy) | HAS | `data/cartography/components/*.json` covers the M3 list; audited per-component in the matrix below |
| descriptor schema v2 validation (variants, defaults, compatibility, layout_constraints) | HAS | `validateComponentDescriptor` |
| composite children grammar (bounded, role-qualified) | HAS | Platform 6.0 Milestone C |
| applicability + accessibility + bounds per component | PARTIAL | layout_constraints + validation exist; accessibility tokens are style-level; M3 audits and fills declared `accessibility` (min contrast/min pt) on the catalog entries missing it |
| catalog drift guard | HAS | `test_knowledge_drift.cpp` + `cartography:lint_catalog` (index regenerated with SICNU_CARTOGRAPHY_REGENERATE_INDEX) |
| accessibility per component | HAS (decision) | real mechanisms are MAP_TINY_FONT + checkStyleContrast; decorative unread descriptor fields rejected as fake compliance |

Per-component audit result (M3): all 12 M3 categories have ≥1 component
(map_frame→map-frame--{primary,comparison,small-multiples}; title/subtitle;
legend×6; north-arrow×3; scale-bar×2; grid×2; source/time→source-note,
text--metadata-block; statistics→statistics--panel; accuracy→accuracy--report;
uncertainty→legend--uncertainty/text--uncertainty-note; chart×12; table×3;
image→publication--panel-image; inset→inset-map--locator; footer→publication--footer;
provenance→text--metadata-block). **No missing category; catalog growth only
where a semantic gap is demonstrated (M3 decision: add `table--accuracy-matrix`
for accuracy E2E, add `chart--confusion-matrix` already exists → verify only).**

## M4 — template composition

| Capability | Status | Evidence |
|---|---|---|
| template inheritance (`extends` chain resolution) | HAS | TemplateRegistry loadProblems (cycles/unknown parents) |
| multi-parent inheritance | HAS (scope correction) | `resolveTemplateChain` folds ordered parent arrays left-to-right with cycle detection + provenance (7.0); M4 PINS it with a disk-catalog test |
| variants + facets (task/medium/purpose) | HAS | Platform 6.0 Milestone D |
| component defaults + overrides precedence | HAS | ADR 0130 chain (item > variant > defaults) |
| semantic template diff | MISSING | M4: `cartography:diff_templates` tool — deterministic structural diff of two resolved templates (slots/defaults/roles/geometry deltas) |
| template provenance (which template+version produced this spec) | PARTIAL | spec carries `template` id string; M4 stamps `{id, version, instantiated_at_commit}` block and compose reports it |
| dangling token/component/style ref guard | PARTIAL | MAP_UNKNOWN_COMPONENT / MAP_STYLE_REF_UNKNOWN exist; token_set refs validated in resolveTokenSet; M4 adds mechanical coverage test asserting every catalog file resolves |

## M5 — thematic cartography

| Capability | Status | Evidence |
|---|---|---|
| continuous/diverging/categorical ramps; colorbars (7 variants) | HAS | style_spec + colorbar components |
| class breaks / classification | HAS | style raster.classification |
| NoData renderer + legend | HAS | 8.0 |
| uncertainty semantics + obligation | HAS | MAP_UNCERTAINTY_NOTE_MISSING |
| bivariate | MISSING (guarded) | only when a semantic contract exists; M5 ships the contract schema + preflight guard, not a renderer without semantics |
| SAR/DEM/optical modality applicability | HAS | checkStyleApplicability (kind/modality/bands) |
| classification legends | HAS | legend--raster-class, legend--categorical |
| temporal/change maps | PARTIAL | legend--change-transition exists; M5 adds a documented change-map style + E2E template |
| accessible contrast | HAS | checkStyleContrast floor |
| scale-dependent style | MISSING | M5: `style.scale_ranges` compiled to QgsRendererRange-based scale-dependent visibility through the existing style compiler (QGIS-native) |
| reproducible renderer config | HAS | style_spec roundtrip + digest |

## M6 — charts / tables

| Capability | Status | Evidence |
|---|---|---|
| chart kinds (bar/histogram/line/area/scatter/pie/sparkline/grouped-bar/time-series/metric-card) | HAS | chart_registry 12 renderers (QPainter, deterministic) |
| tables (table/summary_table/topn_table) | HAS | chart registry + overflow rule |
| confusion matrix | HAS | chart--confusion-matrix |
| class composition | HAS | chart--class-composition/class-area |
| accuracy reporting | HAS | accuracy--report component + statistics panel |
| dual-axis | MISSING (guarded) | M6: explicit `dual_axis: true` declaration required + preflight rule MAP_DUAL_AXIS_UNDECLARED; no silent dual axes |
| text elide/wrap in labels | HAS | 8.0 elide |
| numeric formatting/locale determinism | PARTIAL | M6: pin a deterministic formatter (no locale digit grouping) + tests; current behavior audited |
| no chart silently covering map | PARTIAL | MAP_OVERLAP covers furniture-vs-furniture; chart-vs-map-frame covered by inset rule only; M6 adds explicit rule for non-inset items overlapping map_frames |

## M7 — typography

| Capability | Status | Evidence |
|---|---|---|
| deterministic width model, wrap, kinsoku, ellipsis, shrink-to-fit, line height, break policies | HAS | typography engine 7.0/8.0 |
| CJK+Latin mixed | HAS | width classes |
| overflow evidence | HAS | MAP_TEXT_WRAP_OVERFLOW + fit report |
| real QGIS font/render differences | PARTIAL | visual goldens exist; M7 adds a CJK title golden + font-fallback diagnostic |
| no infinite resize loop | HAS | bounded shrink iterations (12) |

## M8 — QA / repair

| Capability | Status | Evidence |
|---|---|---|
| ~40 rule catalog (invisible layers, legend mismatch, nodata legend, text overflow, off-page, overlaps, contrast, missing source/CRS, atlas, scale/CRS notes, bindings) | HAS | quality.cpp |
| repair convergence (bounded loop; same measurement model) | HAS | repair loop + repairTextOverflow uses fitTextIntoBox |
| repair decision ledger | PARTIAL | CompositionResult.decisions covers solver; repair itself returns count only; M8 adds a typed repair ledger (issue→action→outcome) to the repair tool response |
| typed non-repairable findings | HAS | repairable:false + severity |
| scale/CRS issues | PARTIAL | CRS note rule is text-heuristic; M8 adds map-frame CRS declaration check (`crs` field validated + rule when missing on report docs) |

## M9 — export / reproducibility

| Capability | Status | Evidence |
|---|---|---|
| PNG/PDF via QgsLayoutExporter | PARTIAL | visual tests export PNG; no governed export tool; M9 adds typed `cartography:export` (png/pdf; svg only if QGIS build supports it honestly) |
| DPI/profile | PARTIAL | output block declares dpi 72..1200 + formats; M9 applies at export, print/screen profile tokens exist |
| structural digest + provenance | HAS | structuralDigest + compose provenance |
| atomic export | MISSING | M9: temp-file + rename, digest of written bytes, failure leaves no partial file |
| deterministic render tests / golden | HAS | visual-regression.md methodology + PNG determinism tests |
| missing font/resource diagnostics | PARTIAL | token font fallback applied silently; M9 adds export report entry when QGIS reports missing glyphs/family |

## M10 — harness integration

| Capability | Status | Evidence |
|---|---|---|
| compose/inspect/preflight/repair/confirm typed tools | HAS | cartography_tools (compose, preflight, repair, validate, list_rules…) + harness confirmMapOutput |
| explain tool | MISSING | M10: `cartography:explain` — solver decisions + unsat cores + preflight findings for ONE item/page in a bounded answer |
| Pi decides invocation (no dialog loop in cartography) | HAS | tools are stateless typed tools |
