# ADR 0070: Atmospheric Correction Workflow Integration

## Context

The atmospheric-correction operator (`rs:atmospheric_correction`: DN→radiance,
DOS1/DOS2 histogram-based, QUAC) and its dialog existed, but the dialog forced
manual gain/bias entry and the operator required explicit `gain`/`bias`
parameters — the user had to look up radiometric coefficients by hand, and the
mission's Priority 0 wants sensor metadata to populate parameters
automatically. `rs:atmospheric_correction` also had no operator-level execution
test.

## Decision

1. **Operator gains `metadata_path` and auto-resolved coefficients**
   (`rs_atmospheric_correction_operator.cpp`): when `gain`/`bias` are not
   explicitly provided (and the method is not QUAC, which needs none), the
   radiance gain/bias for the target band are resolved from the sensor metadata
   file — explicit `metadata_path`, else
   `RadiometricCalibration::autoDetectMetadataFile` (ADR 0069) — via
   `loadMetadata` with the band-name mapping from the raster's band
   descriptions. Explicit `gain`/`bias` always win.

2. **Dialog auto-fill** (`atmospheric_dialog.cpp`): on input selection (and on
   band change) the dialog auto-detects the metadata file and fills the
   gain/bias spins with the resolved coefficients, with a status label naming
   the metadata file. Manual edits mark the coefficients "modified", and the
   run then passes explicit `gain`/`bias` (plus the resolved
   `metadata_path`); otherwise it passes only `metadata_path`, letting the
   operator resolve — one shared resolution rule for UI and headless paths.

## Consequences

- The DOS1/DOS2/radiance path no longer requires manual coefficient entry when
  the sensor metadata file sits beside the imagery — matching the mission's
  "sensor metadata populates parameters automatically" for the lightweight
  atmospheric methods.
- Explicit values remain the escape hatch; the operator is testable headlessly
  (new operator execution cases: sibling-MTL auto-resolution and
  explicit-gain-wins).
- QUAC is unaffected (image-statistics based, no coefficients).
