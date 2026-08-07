# ADR 0080: Endmember Extraction (Pixel Purity Index)

## Context

The hyperspectral stack could reduce dimensionality (MNF), match (SAM/SID),
unmix (linear spectral unmixing), and screen anomalies (RX) — but endmembers
had to be supplied by the user. The mission's D7 asks for endmember
extraction (PPI / VCA and/or N-FINDR "where maintainable"); Pixel Purity
Index is the most maintainable pure-C++ option (deterministic, no SVD).

## Decision

1. **Kernel** (`EndmemberExtraction`, `src/processing/algorithms/endmember_extraction.{h,cpp}`):
   `pixelPurityIndex(pixels, count, bands, nEndmembers, projections, result, error)` —
   mean-center, project all pixels onto `projections` seeded-random unit
   vectors (mt19937, fixed seed 42 → reproducible), count how often each pixel
   is a projection extreme, and select the `nEndmembers` pixels with the
   highest counts (tie-broken by index). Returns the endmember spectra
   (endmember-major), the source pixel indices, and the per-pixel counts.

2. **Operator** `rs:endmember_extraction` (`input`, `nEndmembers`, optional
   `projections`): returns the endmember spectra as JSON arrays (plus indices
   and PPI counts) so the output can feed `rs:spectral_unmixing` /
   `rs:sam_classify` directly in a pipeline. Registered; FullRaster policy.

## Consequences

- The hyperspectral workflow is now self-contained: extract endmembers from
  the scene (PPI), then unmix or match against them — with the kernel test
  pinning that the simplex vertices are recovered exactly as the endmembers.
- PPI's hull assumption (endmembers present as pure pixels) is documented;
  VCA/N-FINDR remain possible follow-ups if the pure-pixel assumption fails
  for a given dataset.
