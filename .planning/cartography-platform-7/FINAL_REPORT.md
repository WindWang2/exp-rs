# FINAL REPORT — Cartography Platform 7.0

Branch `feat/cartography-platform-7` (base `origin/master` @ `c731e3e7`, 0 commits behind at PR time).
Worktree `C:/Users/wangj.KEVIN/projects/exp-rs-cartography-platform-7`.

## What shipped (packages A–I of the goal definition)

- **A. Constraint Solver 7.0** (`composition.*`, MapSpec **v4**): hard/soft
  `hardness`, `priority` (0..100), `weight` (soft, 0..1000); canonical
  ordering policy (hard before soft, priority desc, weight desc, declaration
  index) making distinct-key fixpoints declaration-order independent; soft
  phase applies greedily and REVERTS on conflicts with any hard or
  higher-ranked constraint (winner named in the report); bounded unsat-core
  search per unsatisfied hard constraint with honest truncation marking;
  bounded decisions ledger; weighted soft objective. All-hard v3 documents
  keep the 6.0 report semantics (pinned).
- **B. Template inheritance**: `extends` accepts ordered multi-parent arrays
  (parents fold left-to-right, child wins); facets merge (tasks union,
  scalars child-wins); variants merge by id; `inheritance` provenance block
  ({parents, sources}); explicit `extends cycle: a -> b -> a` diagnosis;
  extends shape validated at load. Resolved descriptors carry the child's
  own identity (deep-copied, independently owned).
- **C. Component system**: +5 professional components (text/uncertainty-note,
  statistics/panel, accuracy/report, chart/class-composition,
  publication/footer) with roles, bounds, applicability and `style_tokens`;
  new statistics/accuracy categories; style_tokens shape-validated and
  drift-checked against the token catalog (57 components indexed).
- **D. Typography engine** (new `typography.{h,cpp}`): deterministic UTF-8
  per-codepoint width classes (identical model to the shipped estimator),
  greedy word wrap (Latin words unbroken, CJK kinsoku), hard breaks,
  kMaxWrappedLines budget with honest truncation reporting, truncation
  policies (none/ellipsis/shrink_to_fit/overflow_report), structured
  TextFitReport (+JSON projection), exported budgeted wrap for tooling.
- **E. Style semantics 7.0**: color `scheme` (diverging requires in-range
  center; categorical contradicts continuous), NoData block, uncertainty
  declaration, modality-aware hard applicability checks (SAR single-band,
  multiband ≥3, DEM stretch/classification, uncertainty semantics), class
  ontology mapping (concept-disjoint application refused), deterministic
  WCAG contrast advisories (also surfaced by preflight), canonical repair
  (zero-bracketing diverging derives center=0 with a decision record).
- **F. Charts**: `accuracy_summary` kind — pure derivation of
  overall accuracy/kappa/per-class precision-recall from a square confusion
  matrix (exported + numerically pinned; rows = reference), rendered as a
  plain metric table; dual axis only with a declared semantic justification
  (line/scatter), rendered as a dashed overlay with a reserved right-margin
  tick scale.
- **G. Map QA**: +4 rules — MAP_INVISIBLE_LAYER, MAP_LAYER_UNREFERENCED
  (converging attach repair), MAP_LEGEND_MISMATCH (style class labels),
  MAP_TEXT_WRAP_OVERFLOW (typography model; repair shares the model so it
  provably converges) — plus MAP_CONTRAST_LOW advisory; rule catalog and
  docs updated.
- **H. Visual regression**: `structuralDigest(spec)` — SHA-256 over
  canonical, length-framed, id-sorted geometry (0.01 mm ints, page, z);
  rendering-free known-answer golden pinned; RCA of the headless Windows
  PNG failures (PATH gaps fixed — the executable now runs; the
  QgsLayoutExporter rasterizer stop remains environmental, exit 3,
  documented honestly with the digest as the always-runnable evidence).
- **I. Knowledge drift**: component→token closure (style_tokens resolve
  against the default token set), inheritance-parent closure, style
  applicability modality vocabulary closure, and mutation tests proving
  seeded dangling refs FAIL; index.json regenerated (57 components, facets
  reflecting the union-merge policy).

## Validation evidence (all local, reproducible)

- Configure: Ninja + MSVC 2022, `-j2`; tests `-j1`.
- `[platform7]`: 45 cases — ALL PASSED.
- Full suite `~[visual]`: **145 cases / 1312 assertions — ALL PASSED**
  (post-remediation).
- `[visual]`: repair-to-pass fixtures pass under `QT_QPA_PLATFORM=offscreen`;
  PNG rasterization case terminates the process in this headless session
  (documented; Linux CI lane covers it).
- Catalog drift tests (index byte-equality, gallery listing, token/token-set
  closure, operator/solution closure) all green.

## Review & remediation

Two read-only adversarial subagents (solver/typography/templates/digest;
style/charts/QA/drift/catalog). Pre-remediation verdicts: FAIL/FAIL, 0 P0,
3 P1, 8 P2, 16 P3. **All P0/P1/P2 and all actionable P3 fixed** (commit
`f2c16a1c`); residual P3s accepted with recorded rationale
(.planning/cartography-platform-7/REVIEW_LOG.md).

## Known limitations (honest)

- Headless-Windows PNG export terminates in QgsLayoutExporter (environment;
  PATH component fixed, rasterizer stop remains). Structural digest carries
  the local evidence.
- `style:apply` does not yet push `raster.nodata` into the QGIS renderer
  (validated knowledge; wiring documented as future work).
- Dual-axis rendering requires explicit justification; secondary series is
  line-only overlay.
- Constraints tied on the full canonical key order by declaration index (by
  design; scoped in the header contract).

## Performance / resource notes

- Solver: hard phase unchanged (≤24 passes × O(items+constraints)); soft
  phase ≤8 passes with rank-aware checks; core search ≤32 simulations per
  solve, per-target slices; ledger ≤256 entries.
- Typography: per-codepoint scans, bounded budgets (64 lines, 12 fit
  iterations); no font database, no locale dependence.
- Builds stayed `-j2`, tests `-j1` throughout; no CI used as evidence.
