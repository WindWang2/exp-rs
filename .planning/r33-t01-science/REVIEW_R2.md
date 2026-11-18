# REVIEW R2 — r33-t01-science (R1 fixes applied)

Reviewer: R2 (same independent reviewer as R1). Date: 2026-11-18.
Branch: `zcode/r33-t01-science-20261117T150207Z`, HEAD `2ffa4e3f49` (R1 reviewed through `4292262e57`).
Delta reviewed: `git diff 4292262e57..HEAD` (4 files) plus a re-read of the final full diff `origin/master..HEAD` with fresh eyes on the areas R2 was asked to probe.

---

## 1. Delta verification (R1 findings → fixes)

### P2-1 (R1) — median streaming — RESOLVED, verified correct
`src/operators/opencv/opencv_filter_operators.cpp:254-267` (`neighborhoodRadius` now returns `kernelSize/2` for k>5, validates the kernel there), `:285-292` (comment updated).

**(a) Halo semantics — the bit-identity claim holds, through two independent mechanisms.**

Mechanism 1 (decisive): `runStreaming` does NOT feed the kernel the raw halo buffer — it builds an ROI view of the REAL-data window and calls `applyFilter` on it:

- `src/operators/opencv/opencv_operator_base.cpp:189-198`:
  ```cpp
  const int readX = std::max(0, tile.xOffset - halo);
  ...
  const int readW = std::min(width, tile.xOffset + tile.width + halo) - readX;
  ...
  cv::Mat roi = bufMat(cv::Rect(dstX, dstY, readW, readH));
  applyFilter(roi, params);
  ```
  The ROI spans exactly the raster-clipped rectangle `[max(0,x0−halo), min(W,x0+tw+halo))` — the edge-replicated fringe is EXCLUDED from the Mat. `medianLargeKernel32F` takes `w = img.cols; h = img.rows` (the ROI dims) and clamps taps with `std::clamp(..., 0, w-1/h-1)` — i.e. it replicates the REAL raster border, exactly what the full-frame call's clamp to raster bounds does. For interior tiles the ROI is the whole buffer and core pixels sit ≥ halo = k/2 from the ROI edge, so no clamping occurs at all (full halo of real data); only raster-edge tiles clamp, at the raster border. Core pixels are always inside the ROI (dstX = halo−x0 ≤ halo and dstX+readW ≥ halo+tw follows from x0+tw ≤ W), so the kernel's in-ROI writes cover the core that `opencv_operator_base.cpp:200-201` then extracts (`bufMat(Rect(halo, halo, tw, th)).clone()`).

Mechanism 2 (redundant belt-and-braces): the halo fill IS true BORDER_REPLICATE — `src/processing/gdal/gdal_block_stream.cpp:139-165` replicates the first/last valid pixel of each valid row into the left/right margins, then copies the first/last valid row across the full buffer width; the header contract states it (`gdal_block_stream.h:53-55`). So even if a future change fed the kernel the full buffer, the fringe values would equal the clamped border values.

Determinism of the selection median across tiles: each output pixel depends only on its k×k neighborhood; the tap collection order (nested j,i loop) yields the same value sequence as full-frame (halo data = raster data), and `nth_element` is deterministic for a fixed input sequence → streamed output is bit-identical to full-frame. The new 300×300 seam test (`tests/test_r33_t01_median_large.cpp:238-274`) pins this empirically at the x=254..257 and y=255..256 seams, raster corners/edges, a NaN just past the seam, and the single-NaN-tap pixel (finite, oracle-equal). Passed on my independent re-run.

P2-1 substance confirmed fixed: `context.throwIfCancelled()` now fires per tile (`opencv_operator_base.cpp:173`), memory is one (256+2r)² buffer + per-tile ROI clone instead of the full raster + clone, and k=101 no longer implies an unbounded non-cancellable pass.

**(b) Throw ordering — no crash path before validation.**
`run()` order (`opencv_operator_base.cpp:57-70`): `validateCommonParams` → `requireString input/output` → `fileExists(inputPath)` → `halo = neighborhoodRadius(params)` → `runStreaming` (opens the dataset only afterwards). For an invalid kernel > 5 (e.g. 104 or even), `neighborhoodRadius` throws `InvalidParameter` with the SAME message applyFilter uses, before any dataset is opened or buffer allocated — the exact pattern already established by `oddKernelRadius` for sobel/laplacian (#1044 comment). For invalid kernels ≤ 5, the radius branch falls through to −1 and applyFilter still refuses. Grep confirms `neighborhoodRadius` has no callers outside `opencv_operator_base` / `opencv_filter_operators` (no UI/estimate path can hit the new throw with unvalidated params). `kMaxStreamHalo = 128` ≥ r ≤ 50 — no interaction. Clean.

**(c) #691 NaN-ringtones comment interaction.**
The full-frame rationale ("cv::medianBlur's ordering of NaN inside a window is unspecified and differs between a full image and an ROI view") applies ONLY to the k 3|5 OpenCV path, which still returns −1 and stays full-frame — the comment now sits unambiguously in that branch. The k>5 branch streams precisely because it does NOT use cv::medianBlur: the exact selection median is order-statistic-only, NaN policy is explicit (`isfinite` tap filter, all-NaN window → NaN), and there is no cross-tile state. No contradiction. `test_opencv_operators.cpp` (#691 cases, k 3|5) passes unchanged (1457a/21c).

### P2-2 (R1) — speckle refusal diagnostics — RESOLVED (minimum-change form)
`src/operators/rs/rs_sar_speckle_operator.cpp:242-249`: the refusal message now names the multitemporal companion contract and the exact band number ("companions must share the reference grid and carry band N"). Actionable. Residual nit: the error CODE is still `ErrorCode::GdalError` for what is a user-input contract problem (vs. the network operator's `InvalidInputData`/`GRID_MISMATCH`); D-007 records this as a deliberate minimal-change choice (kernel API stays `bool`). Acceptable — see §2.

### P3-2 (R1) — even-count overflow — RESOLVED
`opencv_filter_operators.cpp:76-81`: the pair is now summed in double (`0.5 * ((double)lower + (double)median))`) then cast — FLT_MAX-scale central pairs can no longer overflow. Exactness for ordinary magnitudes unchanged (the k=7 even-count test still pins 15.0f exactly).

### P3-4 (R1) — docs — RESOLVED
`docs/processing/sar-domain.md:425-432`: §14 now documents the (t₀, 0) velocity anchor with the #2128 rationale and the pair-sentinel row-drop semantics (#2102). Matches the kernel behavior verified in R1.

---

## 2. Deferred P3s (DECISIONS.md D-007) — all safe as documented

1. **Sentinel sample counting** (`rs_sar_remove_topographic_phase_operator.cpp` halo multiplicity): deferring is safe — the result key is honestly named `noDataSentinelSamples` ("samples", not "pixels"), the number is QA-only (never feeds science), and D-007 states the semantics. The displacement operator's unique-pixel counter is the pattern to copy if this is ever revisited.
2. **±Inf tap policy asymmetry at the k 5/7 boundary**: deferring is the right call — the k≤5 path's Inf behavior is OpenCV's (and is pinned by the cv-parity test anchor); forcing `isfinite` semantics there would break that anchor for a case (±Inf pixels) that is invalid data by this repo's own convention (`isMissing`/`!isfinite`) everywhere else. The k>5 policy matches the repo convention. Documented; no action.
3. **Error channel via message text**: acceptable minimum-change resolution; see residual P3-2' below for the recorded code-classification nit.

---

## 3. Full-diff re-examination (anything R1 missed?)

Re-scanned `origin/master..HEAD` (26 files, +2531/−57) with focus on the new streaming interaction and the areas R1 covered. The nine science fixes and their tests are unchanged from R1's verification. New/updated observations:

- **P3 (new, observation — no fix required)**: streamed k>5 computes the median for the halo ring too. `runStreaming` applies the kernel to the full ROI ((tw+2r)² pixels) but writes only the tw² core — at k=101 that is ~1.93× the necessary selections. This is the price of the existing `applyFilter(cv::Mat&)` ROI pattern shared with sobel/laplacian (which can't express "core only"); memory stays bounded and cancellation now exists, so the P2-1 substance is delivered. Note for a future pass: a core-rect overload would halve the worst-case cost.
- **P3 (pre-existing, out of scope)**: `docs/processing/sar-domain.md:429-430` — the sentence "epoch connectivity is per pixel — only the reference component solves, other a per-pair STACK property" is mangled (missing words). Verified present verbatim on `origin/master` (not introduced or touched by this branch); flagging for hygiene only.
- **Residual P3 (accepted, D-007)**: speckle refusal still classified `ErrorCode::GdalError` (message now diagnostic). Recorded as deliberate.

Checked with no finding: `maskBufferToNan` runs over the whole halo buffer pre-filter and the core NaN→sentinel re-stamp post-filter (`opencv_operator_base.cpp:172-210`), so declared-sentinel semantics in the streamed path mirror the full-frame k≤5 write path; the 4×4-with-k=101 fixture (raster smaller than the halo) still passes (ROI = the whole 4×4 raster; clamps at raster bounds); `buf` is allocated once per band and reused; the per-tile `throwIfCancelled` catch-all abandons the output on any exception.

---

## 4. Execution evidence (independent re-run on HEAD 2ffa4e3f49)

Rebuilt `sicnu_operators` and re-ran in `build-dev`:

- `test_r33_t01_median_large` — 234 assertions / 2 cases PASS (matches the claimed count; includes the new 300×300 seam section).
- `test_opencv_operators` — 1457a / 21c PASS.
- `test_r33_t01_speckle_multitemporal` — 29a / 2c PASS.
- `test_speckle_filter` — 15869a / 26c PASS.

(The remaining suites were unchanged since my R1 run of the same tree plus this delta; the request's 22/22 CBeat tally is consistent with 4+1+3+3+2+2+2+3+2 test cases across the 9 new binaries.)

---

## Verdict

**APPROVE.**

Both R1 P2s are properly resolved (streaming verified bit-identical through the ROI-of-real-data mechanism, with replicate halo as a second guarantee; cancellation and bounded memory restored; speckle refusals now diagnostic), the P3 fixes are correct, and the three D-007 deferrals are safe and honestly documented. Remaining items are P3 observations only (halo-ring compute overhead in the streamed median; `GdalError` classification of the speckle refusal; a pre-existing mangled doc sentence on master). No P0/P1/P2 findings.
