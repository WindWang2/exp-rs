# DECISIONS — cn-eo-products-sensor-physics-10

- D-01 (2026-09-13) Sensor truth converges on `data/products/sensor_profiles/` (versioned,
  provenance-carrying). `data/products/band_roles/*.json` keep loading unchanged for
  back-compat; the registry gains the new sensors; a consistency check pins the shared
  keys together. Rationale: no-break migration, single authority for new code.
- D-02 (2026-09-13) New CRESDA-family optical sensors (HJ-2, GF-4, GF-7, ZY-1 02C/02D)
  reuse the existing path-aware scanner + new identity patterns + registry entries.
  CBERS-04 (INPE-format XML) becomes an explicit third sidecar generation with its own
  parser function behind the same ProductMetadata output.
- D-03 (2026-09-13) GF-3 SAR is supported at declared-metadata level: identity, mode,
  polarization, imaging geometry, product level parsed from its sidecar; import stacks
  the (single-polarization) measurement TIFF with typed SAR metadata stamps. No SAR
  processing (calibration to sigma0 etc.) is claimed — numeric-domain stays
  digital_number with an explicit `sar: complex/declared` note when applicable.
- D-04 (2026-09-13) Optional calibration in ImportPlan applies radiance = DN × gain + bias
  only when EVERY requested band carries both declared gain and bias; partial coverage →
  typed refusal listing the bands missing coefficients. Applied calibration flips the
  numeric-domain declaration digital_number → radiance and records the operation in
  provenance.
- D-05 (2026-09-13) Registry carries range midpoint and nominal center as separate
  fields (`wavelength_nm` vs `center_wavelength_nm`); the spectral resampling store stays
  the SRF authority and gains consistency cross-checks rather than being merged.
- D-06 (2026-09-13) Unknown sidecar generations on a recognized family produce
  ProductCompleteness::UnsupportedVersion + bounded raw passthrough of discriminator
  elements, never a parse attempt with the wrong whitelist.
- D-07 (2026-09-13) Registry replaces band_roles entirely (revises D-01's
  "keep loading" fallback): consumers were all in-track, and keeping two live
  sources would preserve the drift the track is chartered to remove.
- D-08 (2026-09-13) Capability pins 111→115: the pinned count is a coverage
  floor, not a historical record; D-08 also repairs #956's inherited break.
- D-09 (2026-09-13) Operator results keep legacy ADR 0157 keys verbatim
  (productKind/bandSource/declared{}/missingDeclaredFields[]) with new
  provenance added alongside; courses and labs key on the old shape.
