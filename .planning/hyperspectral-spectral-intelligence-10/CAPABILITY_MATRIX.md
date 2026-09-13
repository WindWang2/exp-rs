# CAPABILITY_MATRIX — hyperspectral-spectral-intelligence-10

Target end-state matrix (✓ = this track delivers; ○ = pre-existing; ✗ = non-goal).

| Capability | Kernel | Operator input | Workflow artifact | Library input |
|---|---|---|---|---|
| SAM/SID classify | ○ | ○ inline + ✓ table/library ref | ✓ | ✓ `libraryPath`+materials |
| Linear unmixing OLS | ○ | ○ inline + ✓ ref | ✓ | ✓ |
| FCLS unmixing | ✓ | ✓ `method` param | ✓ | ✓ |
| PPI endmember extraction | ○ | ○ | ✓ `endmembersOut` table | n/a |
| Matched filter / ACE | ○ | ○ inline + ✓ `targetRef` | ✓ | ✓ (single entry via select) |
| RX anomaly | ○ | ○ | raster out | n/a |
| MNF forward | ✓ streaming | ○ + ✓ `transformOut` | ✓ transform artifact | n/a |
| MNF inverse | ✓ | ✓ `rs:mnf_inverse` | ✓ consumes transform | n/a |
| MNF-space spectrum conversion | ✓ | ✓ `spectrumRef` | ✓ | n/a |
| Spectral resampling | ○ | ○ | raster | ✓ (seam-internal) |
| Continuum removal / derivatives | ○ | ○ | raster | n/a |
| Band select / bad-band exclusion | ✓ | ✓ `rs:spectral_band_select` | raster | n/a |
| Library subset / sensor projection | ✓ | ✓ `rs:library_select` | ✓ artifact | ✓ |
| Provenance/license machine-check | ✓ loader | ✓ | ✓ digest | ○ validateLibrary + ✓ table rules |

## Known-answer/edge matrix (tests to exist by Phase 8)

| Contract | Test |
|---|---|
| Table digest stability + mismatch refusal | test_spectral_table |
| Measured table without license → refusal | test_spectral_table |
| Size-bound refusal | test_spectral_table |
| Wavelength overlap refusal (disjoint ranges) | test_spectral_reference (operator seam) |
| Unit normalization µm→nm | test_spectral_wavelength |
| PPI→unmix pipeline placeholder flow | test_spectral_pipeline (workflow) |
| MNF roundtrip ≤1e-5; truncated error reported | test_mnf_transform |
| Singular noise covariance → typed refusal | test_mnf_transform |
| FCLS pure-pixel exact; collinear refusal | test_spectral_unmixing (extension) |
| 256-band synthetic cube end-to-end | test_spectral_scale |
