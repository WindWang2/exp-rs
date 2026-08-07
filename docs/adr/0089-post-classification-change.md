# ADR 0089: Post-Classification Comparison (`rs:post_classification_change`)

## Context

Change Detection 2.0 (C1) added continuous change methods (difference / ratio /
normalized difference / CVA), threshold strategies, and morphological cleanup —
but the DoD explicitly lists **post-classification comparison with a per-class
transition matrix** among change-detection outputs. That capability was
missing: no operator compared two thematic rasters and reported how every
class changed between dates (transition matrix, gains/losses, change map).

## Decision

Add a thematic-change operator `rs:post_classification_change` plus a small
pure kernel in `processing/algorithms/post_classification.{h,cpp}`:

- **Kernel** (`TransitionMatrix::countTransitions`): counts valid-pixel class
  transitions into a row-major matrix (row = before class, column = after
  class), with an optional per-pixel validity mask and out-of-range class
  guards; `marginals()` derives per-before and per-after totals. Accumulates
  across calls so callers can feed blocks.
- **Operator** (`RSOperatorMemoryPolicy::MultiPassStreaming`):
  - pass 1 counts transitions block-wise (256-row blocks, O(tile) memory) and
    probes the observed class range;
  - `class_count` defaults to `max observed class + 1` (cap 255 so the
    transition code `before * classCount + after` fits UInt16); an explicit
    `class_count` smaller than an observed class is an actionable error;
  - pass 2 writes the change-type map: UInt16 `before * classCount + after`,
    NoData (65535) where either input is NoData;
  - outputs: `transitionMatrix` (NxN), `fromTotals`, `toTotals`, `netChange`,
    `changedPixels` / `unchangedPixels` / `totalPixels` / `changedPercent`,
    optional `classLabels` echo;
  - grid preflight reuses the shared `compareGrids` service; NoData on either
    side excludes a pixel from both the matrix and the map.
- **Execution seam**: registered in the RSOperator registry + atomic registry
  and exposed as the `tool.rs.post_classification_change` atomic workflow tool,
  so GUI, TaskCenter, CLI, and Agent share one path.

## Consequences

- Post-classification comparison is now a real workflow output: the transition
  matrix answers "what became what", net change per class quantifies gain/loss,
  and the change-type map is a reusable raster (decode with `classCount`).
- Class-coded rasters from `rs:supervised_classification` (or any thematic
  input) can be compared directly; NoData (e.g. clouds) is excluded rather
  than counted as a class.
- The all-invalid case (e.g. an all-cloud scene) is rejected with an actionable
  "no valid pixels" error instead of producing a misleading all-zero matrix.
- Tests cover the kernel (counting, valid-mask, out-of-range, accumulation,
  marginals) and the operator seam (matrix values, change-map codes, UInt16
  NoData, class_count validation, grid rejection, NoData exclusion).
