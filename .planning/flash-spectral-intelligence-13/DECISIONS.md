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

## D3 (revised) — Interference input surface

The shared reference seam (`resolveSpectralReference`) already supports inline
array-of-arrays, spectral-table artifacts and library JSON paths with
wavelength reconciliation, so interference uses `interference` (inline) /
`interferenceRef` (table or library path). `libraryPath` stays reserved for
the target (the seam hardcodes it), so a library-sourced target cannot
silently double as the interference matrix; the driver refuses when neither
interference source is given, and refuses `background` on OSP instead of
ignoring it. Material filtering (`libraryMaterials`) remains target-only —
recorded as a known limitation.

## D8 (final) — Dialog write path

Adopted minimal fix: the dialog fills the v2 provenance fields the strict
loader requires — id slug `profile-<n>`, license `"unspecified"`, citation
naming the measurement panel, synthetic = false — so a saved entry
round-trips through `loadValidated`. The license is deliberately *not*
claimed on the user's behalf (no CC0 assertion on measured data of unknown
license); `material` stays "Untitled" because neither `validateLibrary` nor
any operator enforces the 12-class taxonomy on user files (the taxonomy is
enforced only over the shipped `data/spectral/library.json`).

## D13 — D13 test section disposition

The deleted retriever's test section is *re-expressed* on the authority, not
dropped: analytical SAM ground truth from orthogonal geometry (45°, 0°),
undefined-angle (zero-norm) ordering semantics, exact ordering with a
band-count skip, and constant-spectrum Gaussian-SRF invariance via
`resampleTo`. Two D13 cases (JSON round-trip losslessness, malformed-JSON
rejection) were dropped as exact duplicates of the authority's own tests in
the same file ("round-trips through JSON", "rejects malformed input"); the
maxAngle gating and topK truncation semantics were A-specific API surface
with no authority counterpart (callers truncate) and are not re-added.
The D13 e2e rubrics (Lab02 library top-1) are rewired onto
`SpectralLibrary::matchSpectrum`.

## D14 — Background-raster NoData on the resampled path

When the background grid differs from the scene grid, the tile buffer is
pre-validated (invalid pixels NaN-filled before resampling) and the
accumulators receive null NoData arrays — re-applying the background
sentinel after resampling could exclude pixels whose *resampled* value
coincidentally equals the sentinel.

## D15 — Build parallelism

Initial library build at -j1 (two sibling worktrees already building); after
verifying load ≈ 15/40 cores and RSS ≈ 35%, the build was restarted at the
repository's sanctioned cap -j2 (never -j3+). An intermediate pkill matched
sibling worktrees' `cmake --build` processes as well; their agents own
restarting those builds — recorded here for transparency.
