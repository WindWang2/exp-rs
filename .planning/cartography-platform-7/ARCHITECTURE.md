# ARCHITECTURE — Cartography Platform 7.0

## Authoritative sources (unchanged)

- **QGIS** renders and owns layout truth. MapSpecCompiler → LayoutService →
  QgsPrintLayout. No new render/export path.
- **MapSpec document** (`src/agent/mapspec/mapspec.{h,cpp}`) is the declarative source;
  spec_version 4 in this platform (v4 = strict superset of v3).
- **Knowledge catalogs** under `data/cartography/**` + `data/agent/solutions/**`;
  registries in `src/agent/cartography/` load/validate/resolve them.
- **Execution** stays AgentPlan v2 → WorkflowDefinition (no second scheduler).

## Module ownership (this task only)

```
src/agent/mapspec/            document model, conditions, compiler bridge
src/agent/cartography/        solver, registries, preflight/repair, style, charts, tools
src/agent/layout_tools/       only if a compiler adapter seam must grow (QGIS glue)
data/cartography/**           catalogs
data/agent/solutions/**       solution catalog
docs/cartography/**           reference docs
tests/test_mapspec*           test bundle; tests/CMakeLists.txt target edits
```

Forbidden (owned elsewhere): `src/geospatial/**` (cloud-geospatial-io-7),
workflow/task-center runtime, GUI widgets, QGIS core internals.

## New/changed components (7.0)

### A. Solver 7.0 — `composition.*` evolution (additive, v4 fields)

Constraint items gain optional, backward-compatible surface:

```json
{ "kind": "match_width", "leader": "a", "follower": "b",
  "hardness": "hard" | "soft",            // default "hard" (v3 behavior)
  "priority": 0..100,                       // default 50; hard > soft always
  "weight": 0.0..1000.0 }                   // soft-only, default 1.0
```

Pipeline (extends the 6.0 bounded pipeline, all stages preserved):

```
Parse → Normalize (index, canonical order)
      → Dependency graph + cycle detection            (as 6.0)
      → Hard phase: anchors + hard constraints, bounded relaxation
      → Soft phase: priority-desc, weight-desc, id-asc greedy application
                    (bounded passes; each application reported)
      → Collision handling (avoid_overlap, anchor-aware)
      → Objective accounting: Σ satisfied·weight, Σ violated·penalty
      → Explanation: unsat core = minimal conflicting subset (bounded search)
      → Report: {converged, passes, satisfied[], violated[] with reason,
                 unsat_core[], objective, decisions[]}
```

- **Multi-fixpoint policy**: deterministic canonical ordering — over-determined
  systems resolve by (hardness, priority desc, weight desc, canonical id asc);
  the chosen fixpoint and the alternatives considered are *reported*, so
  declaration order no longer decides outcomes silently.
- **Unsat core**: when a hard constraint is un-satisfiable, run a bounded
  conflict-set search over the constraints touching the involved items
  (≤ kMaxCoreSearchSize subsets, deterministic enumeration) and report the
  minimal conflicting set with per-constraint reasons.
- **Solve budget**: kMaxRelaxationPasses stays (24); new kMaxSoftApplications and
  a wall-clock-free deterministic budget; exceeding any budget is a reported
  diagnostic, never a hang or a silent stop.
- **Anchor authority** (6.0 rule) is preserved and generalized: anchors outrank
  positional constraints; the disabled constraint is reported as `anchor_wins`.

### B. Template inheritance layers — `registry.cpp` (`resolveTemplateChain` + v4 surface)

Layer order (child wins per-field; objects merge, arrays merge-by-key where keyed):

```
base (foundation)  ←  domain (task family)  ←  page/medium variant
                   ←  sensor/task specialization  ←  explicit instance params
```

- Template descriptors may declare `extends: [ids]` (ordered, first-wins per field
  conflicts resolved right-to-left? — NO: left-to-right, later parent wins, child
  last) and `facets` now **merge by union** (tasks) / child-overrides
  (medium/purpose) instead of wholesale replace.
- `variants` deep-merge through the chain (same rule as token variants).
- Cycle detection over multi-parent DAG (already single-parent in 6.0 — extended).
- Migration: existing 56 templates keep working; new `sensor` variants of
  duplicated `_landsat`-style templates collapse via `extends` + `variants`
  where semantics are identical (only where byte-comparable output is proven).
- Every resolution records a provenance list `{field ← source template}` for
  explainability (template chosen / layout reasons must be explainable).

### C. Component system — `data/cartography/components/*.json` + registry validation

New composite/semantic component descriptors (all with role, default bounds,
style-token refs, applicability):

`title.main`+`subtitle`, `legend.composite` (existing, extended),
`colorramp.standalone`, `north.arrow`, `scale.bar`, `grid.coordinate`,
`locator.overview`, `source.note`/`metadata.block`, `uncertainty.note`,
`statistics.panel`, `accuracy.report`, `chart.temporal`, `chart.classcomposition`,
`logo.footer`.

Validation: bounded children (≤16, depth 1 — unchanged), applicability block
schema shared with StyleSpec's, token refs validated at load (drift test).

### D. Typography engine — new `src/agent/cartography/typography.{h,cpp}`

- Deterministic, platform-independent text measurement model (estimator from
  quality.cpp extracted and extended): per-class advance widths (CJK fullwidth,
  ASCII ranges by class, spaces), explicit line-height model.
- Word-wrap simulation: greedy wrap with CJK line-break rules (no break before
  closing punctuation, no break after opening punctuation), max lines, min font,
  min box.
- Truncation policy (declared per item): `none | ellipsis | shrink_to_fit |
  overflow_report` — explicit, never silent.
- Fitting: given rect + text + style → largest font ≤ declared max that wraps
  without overflow (binary search, bounded iterations).
- Output diagnostics: `text_fit_report {item_id, lines, used_width_mm, box_mm,
  truncated, font_pt, policy, overflow_mm}` — consumed by preflight (tiny text,
  clipped title, overflow rules) and by the compiler for `fit_content`.
- Missing-font fallback: token `cjk_fallbacks` + Qt substitution registration
  (unchanged semantics); measurement never depends on installed fonts (determinism).

### E. Style semantics 7.0 — `style_spec.*` additive schema

- `scheme: categorical | sequential | diverging` (+ classification-mode validation:
  diverging requires a declared neutral center; categorical forbids ramp interpolation).
- `nodata: {value?, transparent?, label?, style?}` — validated; style:apply wires it.
- `uncertainty: {kind: none|band|hatch|confidence_interval, field?, level?}` —
  declarative only; application maps to QGIS primitives or reports unsupported.
- Modality applicability packs: SAR backscatter (single-band gray/pseudocolor with
  dB stretch), DEM/hillshade (singleband gray + hillshade render type, azimuth/altitude
  bounds), multiband (band_count ≥ 3, band indices in range).
- Class ontology mapping: `classes[].ontology` optional tag; preflight warns when a
  style declares semantics the target dataset doesn't carry.
- Contrast checks: text vs background luminance ratio (WCAG-derived threshold) and
  adjacent-class distinguishability (ΔE proxy in RGB space) — advisory warnings with
  deterministic thresholds; wrong style → **reject** at validation, or deterministic
  repair only where a canonical fix exists (e.g. contrast floor → token accent swap
  reported as a decision).

### F. Charts — `chart_registry.*` additive kinds + placement spec

- New first-class kinds: `accuracy_summary` (confusion-derived: overall + per-class
  precision/recall bars + kappa), `time_series` (line + temporal axis policy),
  `class_composition` (stacked/grouped bar or pie with ontology labels).
- Dual axis: allowed **only** with `axes: {secondary: {justification: string}}`;
  validation rejects dual-axis without a declared semantic justification; otherwise
  single-axis normalization.
- Placement: MapSpec `charts[]` items carry `bounds` + `anchor` like other furniture;
  Harness can adjust `bounds`/visibility pre-compile (existing patch ops suffice).

### G. QA / preflight / repair — `quality.cpp` rule catalog growth

New rule codes (each: code/severity/repairable/suggested_action, bounded repair):
missing layer ref, invisible/zero-opacity layer, legend–renderer mismatch (class
labels vs style classes), off-page item, overlap (solver-reported), tiny text
(< token floor), clipped title (typography overflow), missing source/time note when
template/solution requires it, uncertainty note required when style declares
uncertainty kind ≠ none, empty output (no map frame / no layers visible).
Repair loop stays bounded (max_iterations); every repair appended to a decision log.

### H. Visual regression — tests + honest evidence

- Structural hash: deterministic serialization of compiled item geometry
  (kind, id, rect rounded to 0.01 mm, z) → sha256 over sorted entries; golden files
  under `tests/fixtures/cartography/structural/`.
- Geometry known-answer tests (no rendering) — always run.
- Compile determinism: double-compile byte-equality (existing pattern extended to
  new surfaces).
- Render golden: root-cause the headless crash (0xC0000135 = STATUS_DLL_NOT_FOUND
  class — diagnose missing plugin DLLs in test environment; try Qt platform offscreen
  + correct PATH assembly). If unfixable in this environment: keep tests tagged,
  document the environmental limitation honestly, and add PNG-render evidence via a
  separate opt-in script that CAN run where QGIS desktop deps resolve. Never claim
  render verification that didn't run.

### I. Knowledge drift — `test_knowledge_drift.cpp` closure checks

Mechanical, catalog-wide (fail on any dangling ref):
- solution → {recipe id, map template id, style ids, report template} resolve
- recipe → operator ids resolve against the operator registry
- template → component ids resolve; slots' recommended components exist
- component → token paths / token set ids resolve (token: refs vs token catalog)
- style → applicability packs reference known modalities/semantics vocabularies
- index.json regenerated + byte-compared (existing) — extended to new surfaces.

## Compatibility

- MapSpec v4 is a strict superset: all new fields optional, v3 documents validate and
  compile unchanged (pinned by tests).
- Catalog JSON: additive fields only; loader rejects malformed but accepts old.
- Tool responses: additive keys only (compact summaries stay within documented bounds).
- Migration doc: `docs/cartography/migration-mapspec-v4.md`.

## Explicit non-goals

- No second rendering engine; no per-glyph OS font metrics in shipped preflight
  (deterministic estimator only); no GUI changes; no workflow/scheduler changes;
  no new agent; no catalog inflation beyond what migration justifies.
