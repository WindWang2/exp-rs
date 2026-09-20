# ADR 0167 — Spectral Intelligence 13.0: TCIMF/OSP detectors, independent background raster, edge-preserving fusion, library consolidation

## Status

Proposed (flash-spectral-intelligence-13)

## Context

Spectral Intelligence 12.0 (ADR 0165, PR #1119) added CEM, mean-window
spectral-spatial fusion, the library-scale MatchIndex and resampling coverage
flags, and explicitly deferred four related gaps. Live census on
`origin/master` `79adfe78` (2026-09-20) confirms all four are still open, and
that no open PR or issue claims them:

1. **No constrained-interference detectors.** MF/ACE/CEM constrain only the
   target (`wᵀt = 1`). Scenes with *known undesired signatures* (the classic
   case: detecting a target against known interference) need the
   target-constrained interference-minimized filter (TCIMF), whose null
   constraints `Sᵀw = 0` suppress the interference exactly, and the
   orthogonal subspace projector (OSP), which removes the entire undesired
   subspace. Neither exists in the tree (only a literature citation in
   `spectral_detection.h`).
2. **Background is always the scene itself.** Every covariance detector
   streams its background statistics from the input raster; the operator
   metadata records "a separate background raster is a future extension".
   There is no way to score one scene against another scene's (or a
   reference panel's) background statistics.
3. **Fusion smears score edges.** The 12.0 fusion is a convex window mean;
   ADR 0165 rejected edge-preserving variants for 12.0 on
   structure-element-semantics grounds, leaving detection scores blurred
   across genuine target/background boundaries.
4. **Two parallel spectral library implementations.** `exp_spectral::
   SpectralLibrary` (`src/core/spectral_library.*`, D13) and
   `SpectralLibrary::Library` (`src/processing/algorithms/spectral_library.*`,
   D12/ADR 0081) are independent retrievers with different entry types,
   metrics, JSON dialects and SRF kernels; a third inline SAM copy lives in
   `rs_library_select_operator.cpp`. The D13 retriever has zero production
   callers.

## Decision

Four work packages, all additive to the existing operator surface except the
library consolidation, which deletes the dead implementation.

### A. TCIMF (`SpectralTcimf`, `rs:tcimf_detection`)

The constrained companion of CEM for known interference:

```
min_w wᵀRw   s.t.  wᵀt = 1,  Sᵀw = 0
⇒  w = R⁻¹(t − S z) / (tᵀR⁻¹(t − S z)),   z = (SᵀR⁻¹S)⁻¹ SᵀR⁻¹t
```

- R is the scene **correlation** (raw second moment) with the same scaled
  diagonal loading and the same fail-closed min-sample floor as CEM
  (`minSamplesRequired`: 2B+2, B+1 with loading) — the estimator is
  identical, so the headroom requirement is identical. The streaming
  accumulator, finalizer and floor are *reused* from `SpectralCem`, not
  re-implemented.
- With k = 0 interference signatures the closed form degenerates to the CEM
  filter with the same arithmetic order — asserted **bit-exact** in the
  kernel tests (a free independent oracle).
- Both constraints are exact by construction: `wᵀt = 1` (target scores
  exactly 1) and `Sᵀw = 0` (every interference spectrum scores exactly 0).
- Typed refusals: non-finite / zero / wrong-width interference spectra;
  `SᵀR⁻¹S` singular (linearly dependent columns under the background
  metric); target inside the interference span (the Schur-complement
  denominator falls to 1e-12 of its unconstrained value).
- Diagnostics: `interferenceCount` plus the true condition number
  λmax/λmin of the small Gram matrix (new
  `sicnu::primitives::conditionNumber`, deterministic power iteration) —
  the family's λmax·B/tr proxy is a *lower* bound and cannot expose
  near-collinear signature sets.

### B. OSP (`SpectralOsp`, `rs:osp_detection`)

```
P = I − U(UᵀU)⁻¹Uᵀ,   w = P d,   score(x) = wᵀx
```

- Consumes **no** background statistics: the undesired subspace is an input,
  so OSP runs a single scoring pass and has no under-sampling refusal. The
  cost moves to the caller, who supplies the undesired signatures through
  the same reference seam as the target (inline array-of-arrays,
  spectral-table artifact or library JSON path via `interferenceRef`;
  `libraryPath` stays reserved for the target so a library-sourced target
  cannot silently double as the interference matrix).
- Scores are signed and **scale with |d|** — documented and asserted,
  unlike the brightness-invariant ACE/CEM.
- Typed refusals: empty interference (a projector onto span(∅)⟂ = ℝᴮ would
  reduce OSP to a bare dot product with no suppression semantics),
  singular UᵀU, non-finite/zero/wrong-width signatures, and a projected
  target retaining < 1e-12 of the target energy (numerically inside the
  undesired subspace).

### C. Independent background raster (WP-B)

The covariance detectors (MF/ACE/CEM/TCIMF) accept an optional `background`
raster for the background statistics. The contract follows the *spectral*
nature of the statistics:

- matching band count, and a wavelength grid reconcilable onto the scene
  grid — background spectra are resampled per pixel with the shared
  linear/Gaussian kernels (Gaussian when both sides carry FWHM, the same
  rule as the reference seam), with invalid pixels excluded exactly as
  declared-NoData pixels are;
- **no** spatial co-registration: size, CRS and grid may differ, because no
  detector performs per-pixel association between scene and background.
  (Per-pixel association would require a shared grid; nothing here does.)

Typed refusals: missing file, band mismatch, scene without a wavelength
grid against a gridded background, disjoint coverage (per-band coverage
`None`), all-NoData background. Diagnostics: `backgroundSource`
("scene" | path), `backgroundSamples`, `backgroundCondition`,
`backgroundResampled`, `backgroundCoverage` ("full"/"partial"). OSP refuses
the parameter instead of ignoring it.

### D. Edge-preserving fusion (`method: "bilateral"`)

`rs:spectral_spatial_fuse` gains a `method` enum (`"mean"` default —
byte-compatible with 12.0 — and `"bilateral"`) with `sigmaRange`:

```
aggregate = Σ_q G_s(p−q)·G_r(s(q)−s(p))·s(q) / Σ_q G_s(p−q)·G_r(s(q)−s(p))
G_s(d) = exp(−|d|²/(2σ_s²)), σ_s = r/2;   G_r(Δ) = exp(−Δ²/(2σ_r²)), σ_r = sigmaRange
```

A neighbor whose score differs from the center by many σ_r contributes
≈ 0, so a step edge is preserved where the mean smears it; in locally flat
regions G_r ≡ 1 and the aggregate reduces to the mean. The bilateral filter
keeps the pure-window-function property, so halo equivalence stays
**bit-exact** (the operator is now halo-streamed, `Streaming` policy,
interior-only write-back, replacing the 12.0 FullRaster limitation). The
NoData contract, the β=0 / r=0 identities and the refusal set are unchanged;
`sigmaRange ≤ 0` or non-finite is a typed refusal.

Rejected alternatives: guided filtering (needs a guidance-image contract),
superpixel segmentation (label identity leaks across tile boundaries and
breaks the halo oracle), morphological opening/closing (rejected in 12.0).

### E. Spectral library consolidation

`src/processing/algorithms/spectral_library.*` (v2 provenance schema, sensor
registry, material taxonomy, ships `data/spectral/library.json`, all
production callers) is the **single authority**. The D13 retriever
(`src/core/spectral_library.*`) is deleted: zero production callers, a JSON
dialect used by no data file, and a hand-rolled Gaussian SRF duplicating
`SpectralResampling`. No compatibility adapter is added — nothing needs one;
the dialect can be re-derived from the authority if a future consumer wants
it. Consequences:

- the D13 test section is re-expressed on the authority (analytical SAM
  ground truth from orthogonal geometry, undefined-angle ordering,
  band-count skip, constant-spectrum Gaussian-SRF invariance via
  `resampleTo`); the D13 e2e rubrics are rewired onto
  `SpectralLibrary::matchSpectrum`;
- `findNearDuplicates` calls the shared `SpectralClassification::
  spectralAngle` kernel (the fourth inline SAM copy is gone);
- the agent tool resolves the library through `resolveRuntimeDataPath`
  instead of a hardcoded relative path;
- the dialog write path fills the v2 provenance fields (id slug, license,
  citation, synthetic=false) so saved entries round-trip through
  `loadValidated` — previously the write path produced entries the strict
  read path rejected;
- `MatchIndex` (12.0) is unchanged in semantics and gains a 10k-entry scale
  oracle: two query resamples for three grids, bit-exact ranking versus
  brute force.

### F. Registration surfaces

`rs:tcimf_detection` and `rs:osp_detection` are registered in
`rs_operators_init.cpp`; scientific-contract rows extend the existing
detector loop; capability-knowledge entries are appended to
`data/agent/capabilities/spectral_transform.json`; the algorithm_meta
sidecars, the D8 capability sidecars (including the two the 12.0 operators
were missing), `determinism_census.snap.json`, `contract_graph.snap.json`
and the pi knowledge page are regenerated with the repository tools
(`--export-catalog`, `capability_knowledge_tool gen-meta`,
`contract_inventory`, page generator) — never hand-edited.

## Alternatives considered

- **TCIMF against the mean-centered covariance (MF/ACE form).** Rejected:
  it would break the empty-interference degeneracy to CEM (the free
  bit-exact oracle) and force a second background pass. The correlation
  form is the standard TCIMF companion of CEM in the literature.
- **OSP with a data-driven undesired subspace (PCA of a background
  raster).** Deferred: it needs a deterministic symmetric eigensolver whose
  conditioning oracle would double the review surface; users can extract
  signatures with `rs:endmember_extraction` today. Recorded as a known
  limitation.
- **Library-backed interference via `libraryPath`.** Rejected: the shared
  reference seam reserves `libraryPath` for the target; interference uses
  `interference`/`interferenceRef` (the latter accepts library JSON paths).
- **Keeping the D13 retriever as an adapter.** Rejected: an adapter for a
  dialect no data file and no caller uses is dead weight; deletion is the
  consolidation.

## Consequences

- `tests/test_algorithm_meta_drift.cpp`'s pinned catalog count rises by two
  (the two new operators), and the two sidecars the 12.0 operators were
  missing appear, repairing `test_capability_surface_parity` on master.
- The census/graph snapshots and the hyperspectral knowledge page
  regenerate; `test_contract_census_11` / `test_contract_cross_surface_11`
  stay green.
- `rs:spectral_spatial_fuse`'s memory policy changes FullRaster →
  Streaming (a documented limitation of 12.0 is removed); its result JSON
  gains `method` (+ `sigmaRange` for bilateral).
- Detection result JSON gains `backgroundSource` (+ `backgroundResampled`,
  `backgroundCoverage` when a background raster is used) and, for the new
  detectors, `interferenceCount` / `interferenceCondition` /
  `interferenceSource`. All additions are additive; existing keys and
  values are unchanged for existing parameter sets.
- Known limitations: OSP has no PCA-derived subspace path; interference
  material filtering via `libraryMaterials` is target-only; the bilateral
  method is O(pixels·(2r+1)²) like the mean.
