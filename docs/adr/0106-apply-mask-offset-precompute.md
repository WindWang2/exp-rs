# ADR 0106: Apply-Mask Mask-Offset Precompute (Review P-03 Remediation)

## Context

The qt-cpp-review pass (ADR 0105) flagged P-03: in `rs:apply_mask`'s
auto-aligned path, the input-pixel → mask-pixel affine mapping
(`mapToMask`, a divide/floor/bounds-check) ran inside the per-pixel loop of
*every* band. With `bandCount` bands the mapping cost multiplied by the band
count, and the `if (sameGrid)` branch also lived in the hot path.

## Decision

The mask mapping is now computed **once per block** into a `maskOffsets`
buffer (flat index into the mask window, `-1` = outside the mask extent /
clear), shared by all bands:

- same-grid blocks: direct window index;
- auto-aligned blocks: `mapToMask` per pixel, but only once per pixel per
  block instead of once per pixel per band;
- blocks fully outside the mask extent: offsets stay `-1`, so the band loop
  is unchanged (pure copy) — the former dedicated copy branch is folded into
  the same loop.

The per-band loop becomes a single flat-indexed test
(`maskOffsets[i] >= 0 && maskBuf[off] > 0`) with the masked count still
accumulated on band 1. Buffers are hoisted to the block level.

## Consequences

- The mapping leaves the per-pixel-per-band hot path; the cost becomes
  O(block pixels) mapping + O(block pixels × bands) indexed tests, which is
  the minimum for a band-wise mask apply. Behavior is unchanged and pinned by
  the operator suite (same-grid, 60 m→30 m auto-align, CRS/NoData rejection,
  outside-extent blocks — 1167 assertions / 38 cases green).
