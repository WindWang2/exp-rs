# ADR 0093: Per-Class Classification Diagnostics and Imbalance Warnings

## Context

The classification pipeline (ADR-0019) already computed per-class validation
metrics (producer's / user's accuracy, F1) and had per-class training sample
counts available at extraction time — but the `rs:supervised_classification`
operator dropped both at the boundary, exposing only the aggregate
`overallAccuracy` / `kappa` / confusion matrix. The DoD classification
deepening asks for sample-count summaries, per-class metrics, and imbalance
safeguards in professional outputs.

## Decision

- `RsClassificationPipelineResult` gains `trainSamplesByClass` (classId →
  extracted sample count, before any holdout split), populated from the
  extraction result.
- `rs:supervised_classification` now returns:
  - `trainSamplesByClass` — sorted array of {classId, samples};
  - `perClassMetrics` — {classId, producerAccuracy, userAccuracy, f1} per
    held-out class when `testSplit > 0`;
  - `imbalanceWarnings` — human-readable warnings (only when non-empty) for
    classes under 10% of the largest class's samples (and a minimum 20-sample
    largest class, so tiny test rasters do not spam warnings).
- The sidecar metadata and compatibility-check surfaces are unchanged; this
  is a pure output-surface completion at the operator boundary.

## Consequences

- Users, the Data Manager provenance view, and Agent/MCP clients can now see
  *which* class is weak: a class with few samples or poor per-class F1 is
  reported explicitly instead of being hidden behind an aggregate accuracy.
- The balanced-fixture regression proves no warning fires for equal classes,
  and a 16-vs-240 sample fixture proves the warning fires with an actionable
  message; both at the operator seam under `SICNU_HAS_OPENCV`.
