# ADR 0114: Radiometric State Metadata and Change-Detection Comparability

## Context

The DoD product model lists "reflectance/radiance state" as metadata a product
or asset should express, and Change Detection 2.0 requires preprocessing checks
for "comparable radiometric state" — differencing a TOA-reflectance scene
against a radiance scene is physically meaningless. Neither existed: calibrated
and atmospherically corrected outputs carried no state marker, and change
detection only checked the pixel grid.

## Decision

- New dataset-level metadata key `SICNU_RADIOMETRIC_STATE` with the value
  domain `radiance`, `toa_reflectance`, `surface_reflectance`,
  `brightness_temperature`, `digital_number`. Helpers
  `SatelliteProducts::setRadiometricState` / `readRadiometricState` read/write
  it through GDAL (empty string = absent/unknown).
- Writers:
  - `rs:radiometric_calibration` records the state matching its `unit`
    parameter (radiance / toa_reflectance / brightness_temperature).
  - `rs:atmospheric_correction` records `surface_reflectance` for DOS/QUAC and
    `radiance` for `dn_to_radiance`.
- Checker: `rs:change_detection` reads both inputs' states; when both declare
  a state and they differ, it fails with an actionable message ("before is TOA
  reflectance vs after radiance — calibrate/atmospherically correct both to
  the same state"). Absent declarations are skipped (unknown is not blocked),
  so legacy rasters keep working.

## Consequences

- Multi-date comparison now guards a second comparability dimension (grid +
  radiometric state) with the DoD-mandated actionable pre-execution error.
- The state rides along in the product metadata lineage: import → calibration
  → atmospheric correction → change detection carries it through the normal
  operator chain (and thus through the DAG, batch, CLI and MCP surfaces that
  share the operators).
- Pinned by tests: metadata round-trip + missing-file handling; calibration
  (radiance) and atmospheric (radiance for dn_to_radiance) outputs carry the
  state; change detection passes on absent/identical states and fails on
  differing ones.
