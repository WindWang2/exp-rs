# ADR 0074: Classification Model Metadata and Compatibility Checks

## Context

The classification pipeline (ADR 0019) persists a superset `.meta.json`
sidecar (method + scaler + class metadata + format version) next to the model
YAML, but it recorded no training feature schema and no validation metrics —
and the predict-only apply path did not check the target raster's band count
against the model, so applying a model trained on 4 bands to a 6-band raster
silently misbehaved. The mission's C2 wants classifier metadata, validation /
per-class metrics, and model compatibility checks when applied to another
raster.

## Decision

1. **Sidecar gains `features` and `validation`** (`rs_classification_pipeline`):
   - `features` — the 1-based training band indices (the model's feature
     schema).
   - `validation` — holdout metrics (overall accuracy, Cohen's kappa,
     per-class producer / user accuracy / F1) when `testSplit > 0`.
   Additive, no version bump: older sidecars load fine with empty sections,
   and `loadModelSidecar` gains the two out-params.

2. **Model compatibility check on apply** (predict-only path): when the
   sidecar records a feature schema and the target raster's band selection
   differs in size, the run fails with a typed `InvalidBand` error naming both
   band lists ("Model X was trained on N features (bands …) but the target
   raster provides M bands (…). Select the same bands before applying.").
   When the caller supplies no band selection, the sidecar schema is adopted.

## Consequences

- Applying a model to an incompatible raster is now a clear, typed failure
  instead of silent misprediction; classifier artifacts are reproducible
  (the sidecar records exactly which bands produced the model and how well it
  validated).
- The main window's model-load path and the sidecar round-trip tests were
  updated to the extended signature; a new pipeline test pins the
  band-count-mismatch failure.
- Follow-ups from C2 remain: per-class sample-count imbalance warnings and
  probability/confidence outputs.
