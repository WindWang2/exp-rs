# ADR 0148: Structured Spectral Artifacts, Library-Driven Operators and the Complete MNF Chain

## Context

The spectral kernel stack is broad but **incomposable**: SAM/SID
(`rs:sam_classify`), linear unmixing (`rs:spectral_unmixing`) and target
detection (`rs:matched_filter`, `rs:ace`) consume only inline JSON arrays;
`rs:endmember_extraction` (PPI) dies as terminal JSON because the workflow
placeholder contract substitutes *string* ports
(`src/workflow/placeholder_grammar.*`, the #727 dual-path resolution); the
validated spectral library (ADR 0146) is GUI-only; MNF is forward-only with
the transform discarded (`ImageEnhancement::mnf` returns components + SNR
only), full-raster in memory, and its 1e-9 noise-eigenvalue clamp silently
pseudo-inverts a singular noise estimate. The D3 lab track registered these
gaps as ISSUES.md H-1/H-2/H-3.

## Decision

1. **Spectral-table artifact** (`SpectralTable`,
   `src/processing/algorithms/spectral_table.*`): one typed serialized form
   for named spectra — kind `exp-rs:spectral-table`, version 1, band-count
   consistency, finite values, optional nm wavelength/FWHM grids, SHA-256
   digest over a canonical round-trip-exact text of the spectra block,
   provenance block, and a machine-checkable license rule: measured field
   tables (`synthetic=false`, `derived=false`) require `license` +
   `citation`; operator-derived tables carry `derived=true` and inherit the
   license story of their recorded `sourceInput`. Anti-abuse bound
   4 Mi cells. Loading is all-or-nothing; validation never truncates to the
   first error.

2. **Composition rides the existing path contract**: producers write the
   artifact and return its path in the result payload
   (`rs:endmember_extraction.endmembersOut` → payload
   `endmembersArtifact`); consumers resolve `$step.port` placeholders to
   that path. The TaskCenter/resume path already resolved payload string
   ports; `WorkflowRuntime::runStepSync`/`runStepViaExecutionPlane` now
   additionally record every non-empty payload string as
   `<stepId>.<port>` so the wizard-session path resolves identically —
   one port-resolution policy, zero grammar changes.

3. **One reference seam** (`rs_spectral_reference_input.*`): all four
   consumers (`refs/refsRef`, `endmembers/endmembersRef`,
   `target/targetRef`) resolve inputs through a single path with precedence
   inline XOR ref XOR libraryPath (ambiguous → typed refusal), content
   sniffing (table kind vs `entries` library), strict library validation,
   optional material filter, and wavelength reconciliation — resample onto
   the input grid (Gaussian SRF when both sides carry FWHM, linear
   otherwise) with disjoint-coverage refusals; without wavelength metadata
   on either side, exact width equality is required and mismatches name the
   missing axis. Resampling uses the existing ADR 0079 kernels.

4. **Complete MNF chain** (`MnfTransform`,
   `src/processing/algorithms/mnf_transform.*`): double-precision, streaming
   (row feeder: mean pass + covariance pass; O(row·bands + bands²) memory),
   same noise convention as ADR 0075 (horizontal shift differences, cov/2),
   forward basis + inverse basis exposed as a digest-verified
   `exp-rs:mnf-transform` artifact. `rs:mnf` streams and emits the artifact;
   new `rs:mnf_inverse` reconstructs band space from a component raster
   (component subset, dropped-mass RMSE via `errorOut`, wavelength metadata
   restored from the model) and converts a single MNF-space spectrum back
   (`spectrumRef`/`spectrumOut`) — the safe-conversion rule for
   PPI-in-MNF-space: exact with all B components, quantified approximation
   under truncation. Singular noise covariance is a typed refusal; the
   legacy `ImageEnhancement` kernel stays untouched.

5. **True FCLS** (`SpectralUnmixing::unmixFcls`): Lawson–Hanson active-set
   NNLS on penalty-augmented normal equations (sum-to-one to ~1e-6,
   reported as QA), fail-closed zero-norm and collinearity refusals.
   `rs:spectral_unmixing` gains `method` (`ols` default — behavior
   unchanged).

6. **Preprocessing + library composition**: `rs:spectral_band_select`
   (explicit bands / wavelength window / bad-band exclusion ranges, unit
   normalization to nm, metadata propagation) and `rs:library_select`
   (material/wavelength/sensor-projection subsets written as validated
   library artifacts, near-duplicate pairs reported by SAM angle, license
   mix reported) — both compose through the same reference seam.

## Consequences

* PPI → MNF-space or reflectance-space → SAM/SID/unmixing runs as one
  workflow (pipeline test with synthetic simplex ground truth).
* Library entries reach the operators with wavelength-aware resampling and
  machine-verifiable provenance/license echo in every result payload.
* MNF memory drops from ~4× raster (FullRaster estimate) to row-streaming;
  >1024-band cubes are a typed refusal (transform artifact size bound).
* The inline-array contracts are unchanged; every new parameter is optional.
* Sidecars under `data/processing/algorithm_meta/` are regenerated via
  `--export-catalog` (generated artifacts, #707).
