# MILESTONES — execution ledger

Each milestone: evidence-first implementation + targeted tests + regression
entry, structured commits (one logical series per milestone group). Test
target: `test_mapspec` (all cartography test files live there) with new
`tests/test_platform9.cpp` appended to the target.

## M0 — Solver/Expression correctness corpus  【done → commit "fix(cartography): M0 solver/condition regression corpus"】

- [x] Re-verify #864/#865/#866/#877 fixes on new master (ISSUE_TRIAGE.md).
- [x] Regression corpus (tests/test_platform9.cpp, M0 section):
  - relaxation rollback restores pre-solve geometry + reports
    non-convergence (old code: kept oscillated geometry);
  - fit_content on zero-size initial rect materializes (#865);
  - `has(path)` operand in comparisons + bare `has(path)` (#866);
  - NaN vs number under ==, !=, <, >= (#877); NaN vs NaN;
  - ±Inf ordering totality; mixed-kind equality/ordering semantics;
  - condition evaluation determinism (repeat 3×, identical ledger).
- [x] Fix P2-1 found during M0: duplicate "failed" ledger entries when
  searchCores re-runs the real sweep after a core search (dedupe via the
  existing decided/failure report keys).

## M1 — Constraint Solver 9.0  【done → commit "feat(cartography): solver 9.0 — oscillation diagnostics, trace, text-driven sizing, scoped re-solve"】

- [x] Oscillation cause attribution (`oscillations` surface + note text).
- [x] Bounded convergence trace (`trace` in result JSON).
- [x] `fit_content.text_ref` text-driven sizing (deterministic model).
- [x] `resolveCompositionScoped` for bounded repair re-solve.
- [x] Tests: oscillation known-answer (two constraints fighting →
  oscillation reported, geometry restored); trace monotonicity/bounds;
  text_ref sizing known-answer; scoped re-solve == full solve on the
  subset's geometry for non-overlapping partitions.

## M2 — Multi-Page / Atlas  【done → commit "feat(mapspec): multi-page 9.0 — master furniture, page breaks, atlas expressions, continuations"】

- [x] `pages[].furniture` materialization with `master_of` provenance.
- [x] `titles[].expression` / `labels[].expression` → QGIS atlas
  expressions (compile error on invalid expression).
- [x] `page_break` constraint kind + validation + compile.
- [x] `continuation` label post-pass with resolved page numbers.
- [x] Page-placement audit for derived furniture (all compile paths
  carry the parent's page — verified by review + test).
- [x] Tests: 3-page E2E (cover/map/appendix) with furniture on every page
  (structural digest known-answer), page_break + keep_with interaction,
  invalid-expression compile failure, continuation resolution.

## M3 — Component System audit  【done → commit "feat(cartography): component catalog 9.0 — accuracy matrix table, accessibility audit"】

- [x] Per-category audit recorded (CAPABILITY_MATRIX M3 section).
- [x] New `table--accuracy-matrix` component (declared accuracy E2E).
- [x] DECISION: no decorative `accessibility` blocks stamped onto
  descriptors — nothing reads them, which would be fake compliance. The
  real accessibility mechanisms are the MAP_TINY_FONT preflight floor and
  the WCAG-derived checkStyleContrast advisories (both shipped); the
  catalog audit confirmed every category maps to existing components.

## M4 — Template composition  【implemented → verify pending】

- [x] SCOPE CORRECTION from the baseline audit: multi-parent `extends`
  (ordered left-to-right fold, cycle detection, provenance stamps) is
  ALREADY fully implemented in Platform 7.0 — pinning it with a disk-catalog
  regression test instead of re-implementing (no-duplication rule).
- [x] `cartography:diff_templates` bounded semantic diff.
- [x] Template provenance stamping + compose echo.
- [x] Mechanical coverage: every catalog token/component/style/template
  reference resolves (test_knowledge_drift extension).

## M5 — Thematic cartography  【done → commit "feat(cartography): thematic 9.0 — scale-dependent styles, bivariate contract, change-map E2E"】

- [x] `style.scale_ranges` compile + validation.
- [x] `style.bivariate` explicit semantic contract (validation-gated).
- [x] Change-map style + template + E2E roundtrip.
- [x] Tests for all three.

## M6 — Charts / Tables  【done → commit "feat(cartography): charts 9.0 — deterministic formatting, dual-axis contract, furniture-over-map guard"】

- [x] Locale-independent numeric formatting in table/statistics renderers.
- [x] `dual_axis` declaration + MAP_DUAL_AXIS_UNDECLARED rule.
- [x] MAP_FURNITURE_OVER_MAP rule + repair.
- [x] Known-answer tests (formatting, dual-axis accept/reject, rule fire).

## M7 — Typography 3.0  【implemented → verify pending】

- [x] SCOPE CORRECTION from the baseline audit: the kinsoku line-end
  opening guard already exists in `breakableGaps` (both directions are
  guarded since 7.0) — pinned with known-answer tests instead of
  re-implemented.
- [x] Ellipsis-truncation evidence pinned (truncated + U+2026 + !fits).
- [x] CJK visual scene already present in the visual harness
  (`cjk-title` fixture); golden comparison stays opt-in by documented
  design.

## M8 — Map QA / Auto-Repair  【done → commit "feat(cartography): QA 9.0 — new rules, repair ledger, CRS declaration"】

- [x] New rules (M8 list) + catalog/docs sync.
- [x] `repairMapSpecWithLedger` + tool surface; repair convergence tests
  extended to the new rules.
- [x] map_frames `crs` validation + MAP_FRAME_CRS_MISSING.

## M9 — Export / Reproducibility  【done → commit "feat(cartography): export 9.0 — atomic governed export, digests, font diagnostics"】

- [x] export.cpp: atomic PNG/PDF export (SVG probed honestly), page
  selection, dpi, sha256 + bytes, missing-font diagnostics.
- [x] `cartography:export` tool + help entries.
- [x] Deterministic render evidence re-run (PNG determinism suite).

## M10 — Harness integration  【done → commit "feat(cartography): harness 9.0 — explain tool, export wiring, help sync"】

- [x] `cartography:explain` bounded surface.
- [x] Help/diagnostic JSON sync (coverage test green).

## Verification gates

- Every milestone: `test_mapspec` full suite green locally before the next
  milestone starts (ctest -j1).
- Final: full cartography sweep + harness-adjacent regressions
  (test_harness_catalog, test_agent_tools_3 — read-only verification they
  still pass; harness sources untouched).
