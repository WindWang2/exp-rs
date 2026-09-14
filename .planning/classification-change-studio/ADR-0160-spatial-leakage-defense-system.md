# ADR 0160 — Spatial Leakage Defense System (空间无泄漏防穿越协议)

- **Status**: Accepted
- **Date**: 2026-09-14
- **Track**: D15 classification-change-studio
- **Decides**: how train/validation/test sample partitioning prevents spatial
  autocorrelation leakage, and how the guarantee is audited.

## Context

Pixel/ROI samples harvested from one scene are spatially autocorrelated: two
samples 3 m apart are nearly the same observation. Random (i.i.d.) splits
therefore overstate accuracy — the model is tested on what is effectively its
training data. The classic remedy for raster-derived samples is *spatial
block cross-validation with a guard buffer*: assign whole spatial blocks to
folds, then strip a buffer zone so no train sample is ever within the buffer
distance of a validation/test sample.

## Decision

### 1. Partition state machine

`SpatialBlockPartitioner::partition()` maps every sample to exactly one
`SampleRole`:

```
Unassigned -> (block role by seeded hash)  -> TrainBlock | ValBlock | TestBlock
TrainBlock sample                          -> Train
Val/TestBlock sample:
    min Euclidean distance to ANY Train sample <= bufferDistance
                                    -> ExcludedBuffer   (guard ring)
                                    -> Validation | Test
```

- Block index: `bx = floor((x - xmin) / W)`, `by = floor((y - ymin) / H)`;
  degenerate extents (zero width/height, single block) are handled by clamping.
- Block→role assignment uses a seeded uniform hash
  `hash(blockKey, seed) mod 1000` compared against cumulative
  `trainRatio` / `trainRatio+valRatio` (permits ratio sets whose sum < 1;
  residual probability falls to Test). Seed 42 default keeps runs reproducible.
- **Isolation invariant (post-condition, asserted by tests)**: for every
  retained (Train, Validation, Test) sample,
  `d(Train, Validation∪Test) > bufferDistance` strictly; `ExcludedBuffer`
  samples belong to no evaluation set. Train ∩ Test = ∅ always.

### 2. Moran's I audit statistic

`SpatialAutocorrelationAuditor::computeMoransI()` uses inverse-distance
weights with hard cutoff:

- `w_ij = 1/d_ij` for `0 < d_ij <= cutoff`, else 0; `w_ii = 0`
- `I = (N / S0) * Σ_ij w_ij (z_i - z̄)(z_j - z̄) / Σ_i (z_i - z̄)²`
- Guard: if `Σ(z_i - z̄)² < 1e-12` (constant attribute) or `S0 == 0`
  (no neighbour pairs inside cutoff) the function returns `0.0` (neutral),
  never NaN/inf. N < 2 also returns 0.0.

### 3. Report

`SpatialSplitReport` carries role assignments plus audit fields
(`minTrainTestDistance` = min distance between Train and non-Excluded
non-Train samples, `+infinity` encoded as 0.0 count semantics when either
side is empty; `spatialAutocorrelationMoranI` over the label attribute).

## Consequences

- Tests must verify the invariant with **independently hand-computed** Moran's I
  on a 3×3 lattice, and the strict buffer inequality on clustered synthetic
  samples (see `tests/test_spatial_block_leakage.cpp`).
- The partitioner is pure (no GDAL/Qt), deterministic under a fixed seed, and
  lives in `src/core/spatial_split.{h,cpp}` (`namespace rs::core`) so both the
  classifier engine and future CV machinery can consume it.

## Alternatives considered

- **Random row/col checkerboard split** — cheaper, but cannot express a
  physical buffer distance; rejected.
- **Spatial cross-validation with convex-hull exclusion** — stronger, but
  overkill for a single-scene workbench; buffer-based isolation chosen.
- **Feature-space stratified split only** — explicitly the failure mode this
  ADR exists to prevent.
