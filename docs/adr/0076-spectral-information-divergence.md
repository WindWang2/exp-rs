# ADR 0076: Spectral Information Divergence (SID) Matching

## Context

The hyperspectral spectral-matching surface had only SAM (`spectralAngle` /
`samClassify` + `rs:sam_classify`). The mission's hyperspectral priorities
list SID alongside SAM; SID captures spectral-brightness differences that
angle-only SAM ignores, making it the standard complement for material
discrimination.

## Decision

1. **Kernel** (`spectral_classification.{h,cpp}`):
   - `spectralDivergence(t, r, bands, nodata)` — symmetric KL divergence over
     the normalized (unit-sum probability) spectra: `SID = Σ p ln(p/q) +
     Σ q ln(q/p)`, scale-invariant for identical shapes. NaN for null input,
     a nodata band, a negative (non-reflectance) band, or a zero-sum
     spectrum; a band present in only one spectrum is skipped (contributes
     nothing).
   - `sidClassify(...)` — mirrors `samClassify` exactly (pixel-major buffers,
     best-reference labeling, -1 for nodata pixels, degenerate references
     skipped, optional per-pixel divergence output).

2. **Operator** (`rs:sam_classify`): gains a `metric` param (`sam` default /
   `sid`); the run dispatches to the matching kernel, the result JSON reports
   the applied metric, and the metadata documents SID's non-negativity
   requirement. The same reference-spectra workflow serves both metrics.

## Consequences

- Hyperspectral matching now offers both standard metrics through one
  workflow (UI/CLI/MCP), with kernel tests pinning SID's exact value, scale
  invariance, and invalid-spectrum handling, plus an operator SID execution
  test.
- No duplicate operator surface: SAM and SID share the schema, reference
  parsing, and band selection.
