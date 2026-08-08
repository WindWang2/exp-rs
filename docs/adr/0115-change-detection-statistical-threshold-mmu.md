# ADR 0115: Change Detection Statistical Threshold and Minimum Mapping Unit

## Context

Change Detection 2.0 (ADR 0072) implements manual / Otsu / percentile mask
thresholds and morphological cleanup. The DoD contract also lists a
"statistical threshold" strategy and "connected component filtering, minimum
mapping unit" post-processing — both were missing. `ChangeDetection::statistics`
already computed mean/stddev, so the threshold needed only a k-multiplier
wiring; the MMU needed a new connected-component kernel.

## Decision

- `thresholdMethod` gains `statistical`: threshold = mean + k·stddev over the
  finite change-magnitude values (new `statisticalK` parameter, default 2.0).
  The effective value is reported via the existing `thresholdUsed` result and
  `SICNU_CHANGE_THRESHOLD` metadata like the other strategies.
- New `ChangeDetection::connectedComponentFilter(mask, w, h, minArea)`: an
  in-place union-find over the 8-connected 1-pixels that zeroes every
  component below `minArea` (0/1 mask; 255 = NoData never modified; `minArea`
  of 0 is a no-op). The operator exposes it as `minAreaPixels` (default 0 =
  disabled) and records `SICNU_CHANGE_MIN_AREA` on the output mask.

## Consequences

- The change-mask path now covers the full DoD strategy list (manual / Otsu /
  percentile / statistical) and the DoD post-processing list (morphological
  cleanup + minimum mapping unit), all on the shared mask kernel.
- Pinned by tests: kernel MMU (components below/at/above the threshold, NoData
  untouched, invalid arguments) and operator integration — statistical
  threshold on a known distribution yields threshold ≈ mean + 2σ with exactly
  the hot pixels flagged; MMU drops an isolated pixel while a 2x2 block
  survives and the mask records the min-area metadata.
