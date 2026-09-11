# REVIEW LOG — Scientific Algorithms 9.0

## Round 0 — self-review (main agent), 2026-09-12

Scope: `git diff 132da5e998..HEAD` re-read file-by-file after the suites
went green. Recorded checks:

1. `fillDepressions` seeding scan is in-bounds by construction (runs only
   for non-perimeter cells); NaN sentinel and declared-sentinel paths
   both excluded before seeding. All-NoData raster still produces an
   empty queue (pinned by existing adversarial test, re-run green).
2. `isCastableFlowCode` gate: valid D8 codes are exactly 0..128; finite
   non-codes (e.g. 0.5) still rejected by the exact-cast check; ±Inf/NaN
   rejected by `isfinite`. UBSan old-vs-new driver:
   baseline aborts at `terrain_flow.cpp:143`, fixed build clean
   (evidence: UBSAN_853_EVIDENCE.txt).
3. SAR: the geographic-DEM refusal block removed during editing was
   restored and is covered by the flatten/correction operator suites
   (both green); per-axis spacing verified against closed forms
   (kernel + operator level).
4. Spectral probe: verified the streaming path compares sentinels in
   float space still; positive-only filter cannot reject legitimate
   data because reflectance/DN domains are non-negative; removed block
   confirmed write-only (isScaledDataset never read).
5. Contracts: +Inf/NaN/negative fall back to unit; declared finite
   positives honored verbatim (existing behavior tests green).
6. Out-of-ownership build fixes reviewed for behavior preservation:
   GDAL count type mirrors the sibling numeric-axis call in the same
   function; run_bridge local `const` drop precedes a store copy.

## Round 1 — read-only subagent reviews, 2026-09-12

Two reviewers dispatched on the full branch diff (max budget, read-only):

- Reviewer A: architecture / correctness / scientific validity.
- Reviewer B: tests / performance / portability / docs-vs-code.

### Reviewer A — verdict: PASS-WITH-FINDINGS

Independently re-derived the Horn/aspect/incidence closed forms, verified
the priority-flood seeding for 1×1 / 1×N / all-NoData /
all-NoData-adjacent rasters, grepped for residual unguarded D8 decode
sites (none), confirmed the removed `isScaledDataset` block was
write-only dead, and **empirically validated the GTiff single-tag
behavior with the system GDAL 3.13.3 Python bindings** (declaration
order NaN→255 persists 255; NaN→255→NaN persists NaN).

| # | Sev | Finding | Disposition |
|---|---|---|---|
| A1 | P1 | The cited reference operator `rs:sar_terrain_correction` declares the incidence NaN AFTER the mask 255 (and `flagIncidence` defaults on): the persisted GTiff tag is NaN, so the 3-band default output reads the mask's 255 pixels back as valid — the #854 defect live in the sibling operator, violating the documented ordering rule. Drift guard checks presence, not order. | **Fixed**: reordered the correction operator (NaN bands first, mask 255 last) + added a 3-band re-open regression (`mask band 2 NoData == 255`, mask pixel values) to `test_scientific_defects_9`. |
| A2 | P2 | Refused scale declarations reported `resolvedBy="declared-metadata"` — indistinguishable from honored. | **Fixed** (self-review remediation, commit `38a9fdd24c`): refused declarations report `default-unit`; pinned in the #873 test case. |
| A3 | P3 | GTiff re-open re-declares band 1 (gamma0) NoData as 255; NaN is detectable by value only. | **Documented**: one-sentence consequence added to `nodata-and-statistics.md` §1.4. |
| A4 | P3 | The `"9999"` digit anchor in the drift guard is coarse (false positives on legitimate constants/comments). | **Fixed**: anchor retargeted to the removed block's identifier (`isScaledDataset`); the file's comment reworded so the anchor stays clean. |
| A5 | P3 | `CMAKE_SOURCE_DIR` runtime coupling in drift tests. | **Accepted, no change**: follows ~10 pre-existing drift suites; failure mode is a false-positive CI failure, not silent breakage; not new debt. |
| A6 | P3 | Operator comment said "Byte mask" while the band is a Byte-valued Float32 band. | **Fixed**: wording aligned ("Byte-VALUED Float32 mask band") in the flatten operator. |

### Reviewer B — verdict: PASS-WITH-FINDINGS

Verified the CMake additions match sibling style, the budget math of the
fallback test (window ≈ 2087×2092 > budget, +4.1% margin, non-binding
operator clamps, spherical latForCol inversion accurate to ≪1 column),
tolerance discrimination power (e.g. #855 delta ≈ 74× the margin), the
absence of tautological tests, and the behavior-preservation of the two
out-of-ownership build fixes. Found the same correction-operator P1
independently.

| # | Sev | Finding | Disposition |
|---|---|---|---|
| B1 | P1 | Correction operator persists NaN as the GTiff tag in its default 3-band configuration — mask 255 reads back valid (same defect as A1, found independently). | **Fixed** (see A1). |
| B2 | P2 | The drift guard cannot see the #854 DECLARATION ORDER — presence anchors pass the mis-ordered operator. | **Fixed**: the new correction-operator read-back regression pins the order behaviorally; the drift test's #854 case now says so explicitly. |
| B3 | P2 | Fallback coverage guarded only by a test-local budget mirror; every behavioral assertion passes on BOTH sampling paths, so a budget change would silently void the coverage. | **Fixed**: the operator now reports `perPixelFallbackPixels`; the test asserts every in-image cell took the fallback path (structural, not mirror-dependent). |
| B4 | P2 | The "every case old-code-fails" header overclaims: the #853 cases pass on pre-fix code without sanitizers (UB is observable only under UBSan). | **Fixed**: header reworded to state exactly which cases fail pre-fix in any build and that #853 is UBSan-evidenced. |
| B5 | P3 | Branch-internal dtype wording contradictions ("Byte mask" vs Byte-VALUED Float32 band). | **Fixed**: drift-test comment, docs §1.4, and the flatten operator comment aligned with `sar_terrain.h`. |
| B6 | P3 | Band 1 re-open reporting nodata 255 is unasserted. | **Fixed**: asserted in the #854 case; docs §1.4 states the consequence. |
| B7 | P3 | Grep anchors lack rationale; identifier-based anchors preferred over literal bans. | **Fixed**: per-anchor comments added; the `"9999"` literal ban replaced with the removed block's identifier (also A4). |
| B8 | P3 | Missing `<algorithm>`/`<cstdint>` includes in the geocode test (transitively provided today). | **Fixed**. |
| B9 | P3 | OWNERSHIP.md seam wording stale (`sicnu_add_test` vs the file's actual `add_executable` blocks). | **Fixed**: wording updated. |
| B10 | — | Resource bounds (≈26 MiB fixture, bounded vectors, RAII temp dir): acceptable, no action. | Recorded in TEST_MATRIX. |

Both reviews' P0/P1: fixed and re-verified. P2: all fixed. P3: fixed except
A5 (accepted with rationale recorded above). Post-remediation full rerun:
11 suites green, 9,511 assertions total (see TEST_MATRIX).
