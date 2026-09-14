# ARCHITECTURE — hyperspectral-spectral-intelligence-10

## The platform problem

Spectral outputs (`rs:endmember_extraction`) die as terminal JSON; spectral
inputs (`refs`, `endmembers`, `target`) accept only inline JSON arrays; the
spectral library (PR #955) is GUI-only. A "PPI → SAM/SID/unmixing" workflow
cannot be expressed as one pipeline. This track turns structured spectral
data into first-class, typed, serializable artifacts that flow through the
existing path-based placeholder contract.

## Core design: the spectral-table artifact (D-1/D-2)

One canonical serialized form for named spectra:

```
kind: "exp-rs:spectral-table"   version: 1
id, bandCount, count
wavelengthsNm[] (optional), fwhmNm[] (optional)
spectra[count][bandCount]  (row-major)
labels[] (optional), materials[] (optional)
provenance { sourceOperator, inputDigest/path, parameters, createdAtMs, libraryId? , synthetic? }
license, citation (optional; required when measured)
digest: "sha256:<hex over canonical spectra block>"
```

* C++ home: `src/processing/algorithms/spectral_table.{h,cpp}` (Qt-free where
  practical, same layer as `spectral_library`). Name: `SpectralTable`.
* Loaders validate: kind/version, band-count consistency, finite values,
  size bounds (count × bands ≤ 4,194,304 cells by default — typed refusal
  above), digest match. Validation failures are typed refusals naming the
  field and entry index; nothing loads partially.
* Provenance/license rules are machine-checked by the loader + validator
  (measured ⇒ license+citation required), satisfying the track acceptance
  criterion.

### Composition without grammar changes (D-1)

Placeholders resolve to *strings from the parent result payload*
(`resolvePlaceholderPort`, `task_center.cpp:1172`, `workflow_session.cpp:130`).
So artifacts travel as **paths**:

1. `rs:endmember_extraction` gains optional `endmembersOut` (path). It writes
   the spectral-table JSON and returns `endmembersArtifact: <path>` in the
   result payload (plus the bounded inline JSON it always returned).
2. A downstream step declares `endmembersRef: "$ppi.endmembersArtifact"`.
   The existing port scan substitutes the path. Zero grammar change; existing
   `$step.output` paths untouched.
3. Consumers (`rs:sam_classify.refsRef`, `rs:spectral_unmixing.endmembersRef`,
   `rs:matched_filter.targetRef`, `rs:ace.targetRef`) resolve the reference
   through ONE shared seam.

### The reference-input seam (WP-B foundation)

`src/operators/rs/rs_spectral_reference_input.{h,cpp}`:

```
ResolvedSpectralReference resolveSpectralReference(
    params, inlineKey, refKey, WavelengthGrid inputGrid, bounds);
```

Precedence (typed refusals, never silent):
1. inline array present AND ref present → `InvalidParameter` (ambiguous);
2. inline array → shape-check per existing behavior (backward compatible);
3. ref path → sniff content: `exp-rs:spectral-table` JSON → load+validate;
   library JSON (`SpectralLibrary::loadValidated`) → optional material filter;
   otherwise typed refusal (unknown artifact kind).
4. Wavelength reconciliation: when both sides carry wavelength grids →
   resample reference spectra onto the input grid (Gaussian SRF when both
   FWHMs exist, else linear; `SpectralResampling` kernels); overlap check →
   typed refusal on empty/negative overlap naming the nm ranges; when either
   side lacks wavelengths → band-count equality required, mismatch refuses
   with both counts + which side lacks wavelength metadata.
5. Resampled rows are labeled (`source: "resampled"`) and provenance carries
   the resample description.

All four consumers share this seam — no per-operator re-implementations.

## MNF complete chain (D-4, WP-D)

New kernel `src/processing/algorithms/mnf_transform.{h,cpp}` — the existing
`ImageEnhancement::mnf/processMnfFile` stays (tests pin it); the new kernel
exposes the transform so inverse becomes possible:

* `MnfModel { mean[B], noiseWhiten[B][B] (=Σn^{-1/2}), signalU[B][B],
  signalS[B] (SNR eigenvalues), bandCount, wavelengthsNm? }`
* Streaming estimation: pass 1 accumulates noise covariance (horizontal shift
  differences, same estimator as ADR 0075 kernel) and signal mean/covariance
  in one tile stream (O(B²) memory); eigensolve via the existing
  symmetric-eigendecomposition utility used by image_enhancement (reuse, no
  third implementation).
* `forward(tile) = signalUᵀ · noiseWhiten · (x − mean)` — per-tile, no full
  raster.
* `inverse(tile) = mean + noiseWhiten⁻¹... ` (i.e. `x = mean +
  Σn^{1/2}·signalU·y`, with `Σn^{1/2}` precomputed into `inverseBasis[B][B]`).
* Component selection: forward writes top-k columns; inverse accepts
  `components`/`dropComponents` (missing components → reconstruction error is
  real and reported).
* Transform artifact: the model serialized as JSON with digest + provenance
  (`transformOut` param of `rs:mnf`); `rs:mnf_inverse` consumes it
  (`transform` param). Singular noise covariance / rank deficiency → typed
  refusal (no silent pseudo-inverse).
* Safe-conversion rule (documented + enforced): endmembers extracted in MNF
  space are converted back via the same linear map — exact when all
  components retained; lossy under truncation, and the operator reports the
  reconstruction error of the converted spectra so the caller can refuse.
  `rs:mnf_inverse` accepts `spectrumRef` (single-spectrum table in, table out)
  for this.

## FCLS unmixing (D-5, WP-E)

`spectral_unmixing.h` gains `method`: `ols` (existing clip+renormalize,
default, unchanged) | `fcls` — Lawson–Hanson NNLS per pixel (tiny E×E
systems) + sum-to-one via the standard augmented single-constraint trick;
guarded: rank-deficient/collinear endmember matrix → typed refusal naming the
collinear set (tolerance stated); zero-norm endmember refusal; SID's
non-negativity requirement stays. Per-pixel reconstruction error already
streams out via `errorOut`; FCLS adds abundance-sum QA to the result payload
(mean |Σabundance − 1| over valid pixels).

## Preprocessing (WP-F)

* `rs:spectral_band_select` (new): band subset by wavelength ranges /
  explicit indices / bad-band exclusion list; propagates WAVELENGTH(+units)
  metadata; normalizes µm↔nm (`WAVELENGTH_UNITS` respected, output nm).
* Shared wavelength-grid helper (`spectral_wavelength.{h,cpp}`): read +
  normalize a raster's per-band wavelength grid → typed refusal when units
  unknown or values non-increasing. Used by the reference seam, band select,
  and (where missing) resample/derivative operators read it via the same
  helper without changing their contracts.

## Library evolution (WP-G, modest)

* `rs:library_select` (new): subset a validated library by material list /
  wavelength window / sensor projection (`resampleTo`) → writes the subset as
  a spectral-table **or** library-format artifact with license/citation
  preserved; QA payload reports near-duplicate pairs (SAM angle below a QA
  threshold, computed by a nodata-guarded cosine kernel in the operator)
  rather than silently deduplicating.

## Integration surfaces (Phase 4 checklist)

GUI (schema-driven forms — verify only), CLI (`--list`, `--schema`,
pipeline runner), MCP (`tools/list` mirrors registry), workflow
(`builtin_definitions.cpp` gains one demonstrative hyperspectral DAG only if
trivially additive — else skip to avoid scope creep), help/metadata
(operator `metadata()` + `data/processing/algorithm_meta/` sidecars for new
operators), provenance (artifact JSON carries derivation; raster outputs
already lineage-tracked by OutputCommitter).

## Determinism grades (per ADR 0124 / validation-policy)

* PPI counts / artifact digests: bit-exact (RNG contract pinned).
* MNF forward/inverse: bit-exact fixed-order float (single-threaded
  fixed-order reductions); roundtrip identity asserted to ~1e-5 relative
  (eigensolver output is tolerance-grade; the *paired* forward/inverse using
  the same model is a deterministic linear map — assert bit-exact roundtrip
  on the composed matrix applied twice? No: assert tolerance 1e-5 and
  bit-exact streaming-vs-serial equality of each direction).
* FCLS: tolerance grade (iterative NNLS) with locked ε; OLS stays bit-exact.
* Resampled references: bit-exact fixed-order kernels.
