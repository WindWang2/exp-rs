# R14-A Independent review log

Reviewer: non-implementer general-purpose subagent (context: fresh, adversarial method mandated:
risk predictions written before reading diffs; all claims verified against real code with
file:line evidence).

## Round 1 — verdict: FAIL (2×P1, 1×P3 actionable; 2×P3 pre-existing observations)

- F1 (P1) image_fusion.cpp:1662 — HPF halo pan read bypassed the wash; declared pan voids passed
  as data under the NaN working sentinel (regression vs master). FIXED in 500dfb923.
- F2 (P1) test_temporal_algorithms.cpp — temporal probe test declared its fill
  (TestScene.declareNodata defaults true) so the reader masked it pre-probe: fake oracle, test
  passed on unfixed code. FIXED in 500dfb923 (declareNodata=false).
- F3 (P3) rs_sam_classify_operator.cpp — refs carrying a band's declared sentinel were no longer
  rejected after the NaN-sentinel switch. FIXED in 500dfb923 (refs washed against the same
  per-band table).
- F4 (P3, pre-existing, no action) spectral_local_rx.cpp — near-zero (not exactly zero) variance
  with loading=0 can still overflow → NonFiniteScore → raster-fatal. Out of this diff; recorded
  as a future-round item.
- F5 (P3, pre-existing, no action) radiometric_qa — raw QA-word band with an in-range declared
  sentinel could fire bits at NoData pixels. Categorical-band semantics; unchanged.

Verified clean: probe parity with the single-scene twin (constants, threshold); diagonal guard's
rejection set is exactly the divide-by-zero set; wash buffer lifetimes; both continuum spectrum
assembly paths use the source band's sentinel; QA wash precedes evaluation and leaves the QA-word
path untouched; all fusion kernels' `== nodata` sites have isnan companions; scope clean; expected
values recomputed by hand.

## Round 2 — pending (after builds/tests run)
