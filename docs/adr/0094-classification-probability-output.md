# ADR 0094: Classification Probability / Confidence Outputs

## Context

The DoD classification deepening asks for "probability outputs" and
"confidence outputs" where the classifier supports them. The backend
interface already declared `predictProbabilities()` (NormalBayes and MLP
implement it; SVM does not), but nothing consumed it: the pipeline's
tile-streamed predict only wrote the class map, so the capability was dead
code at the operator boundary.

## Decision

- `rs:supervised_classification` gains an optional `probabilityOutput` raster
  path: a Float32 per-pixel **best-class probability** map (NoData −1 on
  ignored pixels) plus a `meanConfidence` result (mean best-class probability
  over valid pixels).
- Validation: `probabilityOutput` requires `method=normal_bayes` — SVM cannot
  emit probabilities, and the operator rejects the combination with an
  actionable error (the pipeline additionally guards via a new
  `RsClassifierBackend::supportsProbabilities()` capability flag, so future
  MLP exposure needs no operator special-casing).
- OpenCV's `predictProb` returns *unnormalized* class likelihoods (Gaussian
  PDF values that can exceed 1); the NormalBayes backend now row-normalizes
  them to a proper posterior (rows sum to 1), so values are comparable across
  backends and usable as confidence in [0, 1].
- The probability map is written inside the existing tile-streamed predict
  pass (same grid, one extra RasterIO per tile) — no second full-raster pass,
  and failure paths remove both outputs.

## Consequences

- Users (and Agent/MCP clients reading `meanConfidence`) can now see *how
  confident* each classified pixel is and audit low-confidence regions instead
  of trusting the hard class map blindly.
- Tests pin the seam: NormalBayes on the clean two-class fixture yields
  posteriors > 0.9 with `meanConfidence` in (0.5, 1], and the SVM +
  `probabilityOutput` combination is rejected before any output file is
  written.
