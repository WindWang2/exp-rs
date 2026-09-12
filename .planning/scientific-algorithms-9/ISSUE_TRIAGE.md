# ISSUE TRIAGE — re-verified on `origin/master` `132da5e998`

Method: each issue re-located on the latest baseline by reading the code;
no line-number-trusting. Categories: still-valid / fixed-by-later-merge /
changed-root-cause / duplicate / cannot-reproduce / out-of-scope.

## In scope (owned, fixed by this track)

| Issue | Category | Root cause on this baseline | Fix milestone | Verification |
|---|---|---|---|---|
| #848 | still-valid | priority-flood seeds only the rectangular perimeter; NoData borders ⇒ empty queue, silent 0-fill | M0/M6 | `test_terrain_flow_nodata.cpp`: NoData-border DEM with interior sink — old code fails, new code fills to spill point |
| #853 | still-valid | `static_cast<int>` on out-of-int-range float (UB) in `isDirNoData` + `watershedLabels` guard | M0/M6 | UBSan smoke + regression: dir buffer holding `-3.4028235e38f` must classify as NoData, not UB |
| #854 | still-valid | dataset-wide NaN NoData over Float32+Byte output; kernel's Byte sentinel 255 unreadable | M0/M2 | operator-level test: band 2 NoData == 255, band 1 NoData == NaN on written file |
| #855 | still-valid | Horn denominators use averaged `cellMeters` instead of cellX/cellY | M0/M2 | analytic tilted-plane on anisotropic grid (cellX≠cellY): slope/aspect vs closed form; old code fails |
| #856 | still-valid | probe `std::abs(v)` promotes negative sentinels into scale detection; `isScaledDataset` block (342-377) is write-only dead code with invented `-9999/65535` sentinels | M0/M4 | unit reflectance scene + undeclared −9999 sentinel must stay unit-reflectance; old code fails |
| #873 | still-valid | `declaredScale > 0.0` admits +Inf ⇒ divisor +Inf | M0/M1 | contract unit test: `domainFromDeclaredScale(+Inf)` must refuse; old code fails |

## Adjacent but out of scope (other tracks' ownership)

| Issue | Category | Rationale |
|---|---|---|
| #874 | out-of-scope (adjacent) | `RasterReader::readMask` is `src/geospatial` (Track-3 seam authority); this track consumes, does not modify. Recorded as a cross-track seam risk for M1: our kernels own their sentinel comparisons. |
| #875 | out-of-scope | `src/dataset/split` — dataset/experiment track's surface. Not a scientific-kernel defect. |
| #877, #866 | out-of-scope | `src/agent/mapspec` — agent track. |
| #862, #851, #876, #860 | out-of-scope | scheduler/concurrency — execution track 9. |
| #857, #858, #859, #861 | out-of-scope | Qt workbench / display — workbench track. |
| #863–#865, #868, #878 | out-of-scope | cartography solver — cartography track. |
| #867 | out-of-scope | agent harness recipe catalog. |
| #869–#871, #880–#882 | out-of-scope | help/diagnostics catalog — help track (operator-side schema text is re-anchored by our drift tests, not by editing those catalogs). |
| #872, #879 | out-of-scope (adjacent) | operator *schema* text drift (rs:infer / fusion aliases) lives in registry/UI contract files shared with the help track; the scientific semantics this track owns are unaffected. Recorded as follow-up cross-track report. |
| #850, #849, #852 | fixed-by-later-merge (unpushed) | local `main` commit `8f6293bceb` claims these; not part of this baseline and not our ownership — noted for the PR description. |

## Historical leads from the /goal prompt

- #854, #855, #856: **still-valid**, fixed here (above).
- #848: **still-valid on this baseline** (an unpushed local fix exists on
  `main`; this track's fix is independently derived + tested here).
