# DECISIONS — flash-spectral-intelligence-13

Autonomous decisions taken during the track (no human in the loop). Each records
candidates, the trade-off, and the adopted default.

## D1 — Kernel placement and sharing

- Candidates: (a) new standalone kernels per detector; (b) extend `spectral_cem.*`
  with interference/OSP parameters; (c) new files reusing CEM's accumulator.
- Adopted **(c)**: `spectral_tcimf.{h,cpp}` and `spectral_osp.{h,cpp}` reusing
  `SpectralCem::accumulateCorrelation/finalizeCorrelation/minSamplesRequired` and
  `sicnu::primitives::invertDenseMatrix`. Rationale: the 12.0 skeleton was designed
  for exactly this ("TCIMF is a documented future extension on the same skeleton",
  ADR 0165:99-101); separate files keep each detector's contract reviewable and
  avoid bloating CEM's fail-closed semantics. Rejected (b): parameter-flag kernels
  make the scientific contract ambiguous and the refusals untestable per detector.

## D2 — TCIMF background matrix form

- Candidates: (a) covariance Σ (mean-centered, like MF/ACE); (b) correlation/second
  moment R (like CEM).
- Adopted **(b)**: TCIMF is the constrained companion of CEM in the literature
  (Manolakis et al. 2014), and sharing R makes the empty-interference case reduce to
  CEM exactly — a free independent oracle (O1.2). Mean-centering would break that
  identity and force a second background pass. The MF/ACE covariance path stays
  untouched; TCIMF's doc states the form explicitly.

## D3 — TCIMF interference input surface

- Candidates: (a) inline spectra only; (b) library material references only;
  (c) both.
- Adopted **(a) inline spectra with optional wavelength grid**, reconciled through
  the same `resolveSpectralReference` seam as the target. Rationale: YAGNI —
  library-backed interference is a thin wrapper over inline once the kernel exists,
  and no caller needs it today; the inline form is fully testable (O1). Recorded as
  a known limitation.

## D4 — OSP subspace input surface

- Adopted: interference/undesired signature matrix inline (spectra + optional
  wavelengths), same seam. A data-driven subspace (PCA of a background raster) was
  considered and **deferred**: it needs a deterministic symmetric eigensolver
  (~100 lines) whose conditioning oracle would double the review surface; users can
  extract signatures with the existing `rs:endmember_extraction`. Known limitation,
  follow-up candidate.

## D5 — Background raster compatibility contract

- Rule adopted (from the track prompt): spectral statistics only ⇒ wavelength grid
  must be reconcilable (resample background spectra onto the scene grid; NoData
  pixels excluded), but **no** shared CRS/size/grid requirement. Per-pixel
  association is not performed by any detector, so it is not required. A
  different-size background raster is legal (O3.4). Refusals stay typed for band
  mismatch, disjoint coverage, all-NoData, malformed metadata.
- Score-pass policy: unchanged from 12.0 (declared-NoData pixels excluded from
  background statistics but still scored; documented intentional in
  test_spectral_detection_streaming.cpp:10-13). The background raster does not
  change the score-pass predicate — recorded, not silently "fixed".

## D6 — Fusion method surface

- Adopted: `method` enum on the existing kernel/operator: `"mean"` (default,
  byte-compatible with 12.0) and `"bilateral"` (spatial Gaussian σ_s = r/2 by
  convention with a `sigmaRange` parameter). Rejected: guided filter (needs a
  guidance image contract), superpixel segmentation (label-identity leakage across
  tiles breaks the pure-window-function property and with it the halo oracle),
  morphological opening/closing (rejected in 12.0 for contested element semantics).
  Bilateral keeps the pure-window-function property → halo equivalence holds
  bit-exactly (O4.5), which is the track's tile-streaming oracle.

## D7 — Library consolidation direction

- Adopted: `src/processing/algorithms/spectral_library.*` (v2 provenance schema,
  sensor registry, taxonomy, ships `data/spectral/library.json`, all production
  callers) is the single authority. `src/core/spectral_library.*` is deleted: it
  has zero production callers, its JSON dialect is used by no data file, and its
  hand-rolled Gaussian SRF duplicates `SpectralResampling`. Its two test-only
  call sites are rewired onto the authority API preserving test intent. MatchIndex
  (`spectral_library_scale.*`) stays — it is already built on the authority's types
  and is the scale path. No compatibility adapter is added (nothing needs one);
  if a future consumer needs the D13 dialect it can be re-derived from the
  authority. `findNearDuplicates`' inline SAM is rewired to the shared kernel.

## D8 — Dialog write path

- The dialog saves ad-hoc profiles without id/license/citation and material
  "Untitled", which `loadValidated` consumers reject — a genuine write/read schema
  disagreement. Adopted fix (minimal, additive): the dialog fills the v2 required
  provenance fields (synthetic id slug, CC0 license, synthetic=true) and writes the
  material from the taxonomy; the profile keeps its measured spectrum. UI behavior
  otherwise unchanged. If review shows the material field cannot be satisfied
  honestly for an arbitrary profile, fall back to documenting the limitation.

## D9 — Pre-existing build breakage

- `src/cli/cli_commands.cpp` reportedly failed to compile on master at 12.0 time.
  If the first full build shows it still broken, this track does NOT fix it (out of
  ownership); the affected test targets are excluded from the targeted set with the
  evidence recorded in the ledger and the PR body.

## D10 — ADR number

- 0166 is claimed by open PR #1136. This track takes **0167**; if a conflict
  appears at PR time, the ADR is renamed to the next free number and the ledger
  records it.

## D11 — Sequencing with PR #1135

- #1135 regenerates `determinism_census.snap.json`, `contract_graph.snap.json`,
  the two spectral capability sidecars, `pi/knowledge` page and the meta-drift pin.
  Adopted: develop on this branch independently; at PR time fetch latest master,
  merge/rebase if #1135 landed, regenerate ALL surfaces with the repo tools from
  the union tree (never hand-merge JSON), and re-run the gates. Conflict risk on
  `tests/CMakeLists.txt` is low (append-only tail, disjoint insert points).

## D12 — Streaming for the fusion operator

- The 12.0 operator is FullRaster (documented limitation). Adopted: convert to
  halo streaming (r-pixel halo, interior-only write-back, the local-RX pattern) as
  part of the bilateral work, so the operator-level tile oracle exists for both
  methods; if the conversion proves unstable under review, revert to FullRaster and
  keep kernel-level halo equivalence only (still satisfies O4.5 at kernel level).
