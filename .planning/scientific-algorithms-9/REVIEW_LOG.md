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

**Status: reports pending.** Their verbatim findings and per-finding
dispositions will be recorded here when the reviews return — nothing is
pre-written; every disposition will cite the finding's evidence and the
remediation (or the reasoned acceptance) actually applied.
