# FINAL REPORT — Scientific Algorithms 9.0

## Baseline & problem statement

Audited `origin/master` `132da5e998` (PR #847). A 2026-09-11 defect wave
(#848–#882) contained six scientific-correctness defects inside this track's
ownership (#848, #853, #854, #855, #856, #873); all six were re-verified on
the baseline by reading code (not issue line numbers) and confirmed
still-valid (ISSUE_TRIAGE.md). The 8.0 track's known-limitation list
additionally flagged the `rs:sar_geocode` over-budget source-window fallback
as bounded-but-untested — a corpus gap taken up here.

## Delivered

1. **M0 — six scientific defect fixes**, each with old-code-fails regression
   coverage and, where numeric, closed-form independent references:
   - #848: priority-flood fill seeds the NoData-adjacent boundary (the
     rim-only seeding silently filled nothing on NoData-bordered DEMs);
     NoData-adjacent cells are drainage-boundary cells, never raised.
   - #853: D8 direction decoding range-gates float→int casts (UB with GDAL
     Float32 sentinels); UBSan old-vs-new evidence committed in the planning
     corpus.
   - #854: per-band NoData on the SAR flatten output (NaN + mask 255), plus
     the sibling `rs:sar_terrain_correction` whose 3-band default persisted a
     NaN tag — found in review, fixed, regression-tested.
   - #855: per-axis Horn spacing in SAR terrain (2:1 grids skewed slope and
     rotated aspect ~19°); analytic known-answer at kernel and operator E2E
     level (gamma0 matches the closed form to 1e-3 through the production
     path).
   - #856: positive-only scale probing; removed the dead re-probe block with
     invented sentinel constants; E2E EVI known-answer immune to undeclared
     negative sentinels.
   - #873: declared scales must be finite; refused declarations report
     `default-unit` provenance.
2. **M1 — semantics contract enforcement**: `test_semantic_drift_9` binds the
   contracts to source anchors; `docs/processing/grid-and-radiometric-policy.md`
   (§2.5–2.6, §2a) and `docs/processing/nodata-and-statistics.md` (§1.4–1.5)
   codify the 9.0 rules including the GTiff tag-ordering rule.
3. **M2 — SAR corpus**: anisotropic closed-form suite + first coverage of the
   geocode fallback (2600×2600 scene; `perPixelFallbackPixels` observability
   added to the operator; linear-field round-trip exact through the fallback).
4. **M3/M5/M7 audits**: executed suites as evidence (radiometric calibration
   195,688 assertions; atmospheric 37,486; temporal fit/core 529; accuracy 15)
   — no verified gap inside this track's scope; refusals (InSAR, quad-pol,
   "physical-level atmospheric correction") stand as documented.
5. **M6**: #848/#853 complete the terrain family; `terrain_analysis`'s
   anisotropic contract was already anchored by the #612 suite (1,450
   assertions re-run green).
6. **M8**: targeted benchmark (PERFORMANCE.md) — the #848 seeding costs
   nothing on NoData-free rasters (2038 ms vs 2283 ms baseline at 4 M cells,
   -O2); on 20 % NoData rasters the fixed flood does the work the defect
   skipped (3087 ms vs 960 ms that silently filled nothing).
7. **Out-of-ownership, disclosed baseline build fixes** (GDAL 3.13
   `GDALMDArrayRead` count type; `experiment/run_bridge.cpp` const) —
   behavior-preserving, review-verified.

## Adversarial review

Two read-only subagent reviews (architecture/correctness/scientific-validity
and tests/performance/portability/docs-vs-code), both PASS-WITH-FINDINGS.
All P0/P1 and P2 findings fixed and re-verified; P3s fixed except one
accepted-with-rationale (CMAKE_SOURCE_DIR coupling follows ~10 pre-existing
drift suites). Highlights: Reviewer A empirically validated the GTiff
single-tag behavior with GDAL 3.13.3 bindings and caught the live #854
ordering defect in the sibling operator; Reviewer B independently found the
same P1, demonstrated the fallback test's coverage was mirror-dependent
(now backed by an operator-reported counter), and verified docs-vs-code
claims line-by-line. Full log: REVIEW_LOG.md.

## Test evidence (executed locally, post-merge, post-remediation)

clang 22.1.8, Debug, GDAL 3.13.3, offscreen Qt; 11 owned/affected suites all
green, 9,511 assertions total — per-suite counts in TEST_MATRIX.md. Merged
`origin/master` `f316dfdbb4` (the parallel #853–#882 sweep) before the final
rerun; conflicts resolved toward the stricter fix with the merged suites as
arbiter (rationale per file in the merge commit — notably the sweep's
INT_MAX-based gate has a residual UB hole at d = 2³¹ exactly, fixed by the
[0, 128] gate).

## Known limitations / follow-ups

- #874 (`RasterReader::readMask` Float32 sentinel equality) and #875
  (SplitEngine folds) are adjacent but outside this track's ownership —
  recorded as cross-track items in ISSUE_TRIAGE.md.
- #872/#879 (operator schema text drift on rs:infer / fusion aliases) belong
  to the help/registry track; this track's drift tests cover scientific
  semantics, not catalog text.
- The mask band is a Byte-valued Float32 band (single-dtype streaming
  writer); a true per-band-dtype writer would remove the GTiff tag-ordering
  constraint entirely — a geospatial-seam follow-up.
- Sanitizer run of the full suite remains unplanned; #853 has targeted UBSan
  evidence.
- GTiff single-tag means float bands' NaN NoData is value-detectable only on
  re-open (documented; formats with per-band metadata would lift this).

## CI/CD statement

Online CI/CD was not required and was not waited on; completion is based on
the locally executed, reproducible evidence recorded in TEST_MATRIX.md,
PERFORMANCE.md, UBSAN_853_EVIDENCE.txt, and REVIEW_LOG.md.
