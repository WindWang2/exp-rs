# PR_BODY — Spectral Intelligence 11.0

> Local evidence only; no online CI dependency. Baseline `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`.

## Summary

Four spectral capability gaps closed end-to-end (kernel → operator → artifact/capability surface → GUI), all in **new files** with append-only edits to shared registration surfaces, plus an honest scale/failure evidence layer. ADR 0163.

- **A — Local dual-window RX** (`rs:local_rx_anomaly`, `SpectralLocalRx`): Reed–Xiaoli distance to the LOCAL background with inner guard window, raster-clamped windows (no replicated border pixels), scaled diagonal loading `α·tr(Σ)/B`, full & diagonal covariance modes, per-pixel background-sample quality plane; under-sampled windows stay **NaN (unscored), never faked**; full-covariance mode refuses > 8192 bands. Operator streams padded-halo tiles: interior scores equal whole-raster scores (tile-agnostic), score + optional quality raster, `abandon()` cleanup on cancel/failure.
- **B — Sparse unmixing** (`rs:sparse_unmixing`, `SpectralSparseUnmixing`): FISTA with the exact prox of ℓ1+non-negativity (`max(v−λ·step,0)`), FCLS-convention sum-to-one penalty (ρ·11ᵀ), **overcomplete dictionaries** (atoms > bands, cap 2048 = 32 MiB Gram), deterministic fixed-sweep Lipschitz estimate, fail-closed near-collinear refusals (SAM threshold, 0 disables), per-pixel `converged`/`iterations` honesty, reconstruction RMSE + Σa QA.
- **C — SID-SAM hybrid similarity** (`rs:spectral_similarity`, `SpectralHybridSimilarity`): bounded `product_normalized` (sam′·sid′ ∈ [0,1]) + classic `SID·tanθ`, over master's single-source SAM/SID kernels; wavelength-grid comparability guard (disjoint grids refuse); fail-closed form parsing; Float32 labels (−9999 unlabelled) + optional score raster.
- **D — Endmember analysis** (`rs:endmember_analysis`, `EndmemberAnalysis`): average-link SAM clustering (PPI-ranked representatives, deterministic ties), symmetric pairwise angle matrix, Gaussian-SRF sensor projection via the master resampling seam (wavelength metadata mandatory; coverage flags; `requireFull`); output is a **derived `exp-rs:spectral-table`** — no second artifact authority — with inherited license/citation and fresh digests.
- **F — Spectral Workbench 11** (`SpectralWorkbenchPanel`): independent dock (hidden by default, Window-menu toggle) consuming spectral-table artifacts: spectra list, SAM matrix view, provenance/digest/license status, `spectrumSelected(label, index)` linkage seam. No D18 mission-workbench or #1008-contested files touched.
- **G — Surface**: both operator registration sites, `algorithm_meta` task families (`anomaly-detection`, `unmixing`, `classification`, `endmember-analysis`), capability JSON entries with honest limitations, drift-gate pins 32 → 36.
- **H — Scale evidence** (`test_spectral_scale`): 1024-band closed-form sparse unmixing with **bit-identical** repeat runs; 1024-band diagonal local RX vs an independent test-local reference + determinism; 1024-band × 256-atom overcomplete dictionary; full-covariance refusal at 8193 bands; wall-clock never a gate.

## Dedupe / parallel ownership (startup audit, refreshed)

- `origin/master` advanced during the track: #991 (D18 workbench) and #992 (D19 foundry) merged before worktree creation; baseline refreshed to `a5b11b7f`.
- **Open PR #1008 (`zcode/radiometric-spectral-workbench`, CONFLICTING at startup)** owns `spectral_unmixing.*`, `spectral_indices.*`, `spectral_profile_widget.*`, `src/core/spectral_library.*`, continuum/6S/radiometric files. This track treats all of them as **read-only**: complementary capabilities (sparse vs FCLS, hybrid vs SAM/SID, local vs global RX) land in new files; shared surfaces (tests/CMakeLists, app CMakeLists, .gitignore) get minimal appends. Full matrix: `.planning/spectral-intelligence-11/PARALLEL_OWNERSHIP.md`.
- Open issues #1001–#1007 audited: all dataset/workflow/georef/io-domain, none spectral — not duplicated here.

## P0/P1 (out of scope) disclosure

- **P1, pre-existing on master, minimally fixed**: `src/workflow/pipeline_run_coordinator.cpp` (from merged #991) uses `_wopen/_O_WRONLY/_O_BINARY` without `<fcntl.h>/<io.h>` — `sicnu_workflow` does not compile on Windows at baseline, blocking `sicnu_geo_rs_cli --export-catalog` (capability regeneration). Fix: Q_OS_WIN-guarded standard includes only; no semantic change. Evidence: build log with `error C2065 '_O_BINARY'` at `pipeline_run_coordinator.cpp(55)`; no open PR owns this file at creation time.

## Compatibility

- Existing operators unchanged (`rs:rx_anomaly` remains global; `rs:spectral_unmixing` remains OLS/FCLS); all new ids are additive (`rs:local_rx_anomaly`, `rs:sparse_unmixing`, `rs:spectral_similarity`, `rs:endmember_analysis`).
- `exp-rs:spectral-table` format untouched (v1): endmember analysis emits a conforming derived table; placeholder path contract (`$step.port`) untouched.
- Capability JSON: additive entries; `kAllIntents` untouched (no new intents).
- GUI: additive dock, hidden by default; no existing-widget behavior change.

## Local tests (all exit 0, offscreen, `-j1`)

| Suite | Covers |
|---|---|
| `test_spectral_sparse_unmixing` | identity-dict soft-threshold closed form; orthogonal per-atom closed form; pure pixel; overcomplete dict; collinear/zero/NaN refusals; cap; honest non-convergence; batch=per-pixel |
| `test_spectral_local_rx` | independent window-enumeration reference (Full & Diagonal, interior/edge/corner); injection dominance; guard semantics; NoData exclusion; min-sample unscored honesty; validation refusals |
| `test_spectral_hybrid_similarity` | exact `arccos(4/5)` / `(2/3)ln2` pair; scale invariance; orthogonal bounding; nodata unscorable; grid guard refusals; classification; fail-closed form parsing |
| `test_endmember_analysis` | angle matrix symmetry/values vs independent computation; hand-built cluster merge + PPI leaders; threshold-0 passthrough; degenerate refusals; linear interpolation exactness; constant-spectrum Gaussian invariance; coverage flags/refusal |
| `test_spectral_scale` | 1024-band closed forms; bit-identical determinism (memcmp); 1024×256 dict; 8193-band refusal; hybrid/matrix/reduce at 1024 bands |
| `test_spectral_pipeline_11` | registry-level chain PPI→analysis→sparse with provenance/digest custody per hop; local RX anomaly recovery + quality plane; hybrid classification on rasters |
| `test_spectral_workbench_panel` | offscreen widget: validated load, selection linkage signal, clamped selection, fail-closed broken artifacts |
| `test_algorithm_meta_drift` | pins 32→36 (+4 task-declaring operators); sidecars regenerated via `--export-catalog` |

Run pattern: `cmd //c sic11-test.cmd <targets>` with vcvars64 + Qt bin + `QT_QPA_PLATFORM=offscreen`; resource caps `CMAKE_BUILD_PARALLEL_LEVEL=2`, Ninja `-j2`, `CTEST_PARALLEL_LEVEL=1`. Load-average metric not measurable under Git Bash (recorded once in EVIDENCE); RSS spot-checked via tasklist during builds.

## Known limitations / follow-ups

- Full-covariance local RX is O(w²B²)/px — documented; diagonal mode covers high band counts (subspace/FFT acceleration = follow-up).
- Sum-to-one in sparse unmixing is a penalty (same convention as master FCLS), QA reports |Σa−1|.
- `classic_tan` diverges for orthogonal spectra (+inf, documented form property).
- Endmember reduction caps at 512 rows; angle-matrix embed at 64.
- Operator-level cancel covered by `throwIfCancelled()` per tile + `abandon()`; kernel-level cooperative cancel not added (batch kernels are bounded-call).
- algorithm_meta sidecar regeneration requires a local Windows-capable toolchain; the drift gate now pins 36.

## Review findings

See `.planning/spectral-intelligence-11/REVIEW_LOG.md` (independent adversarial review + dispositions; P0=0, P1=0 at PR creation).
