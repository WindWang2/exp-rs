# BASELINE — hyperspectral-spectral-intelligence-10

* Baseline SHA: `7d78059d1a6d316d606656759a506d17bc5e3b55` (origin/master, 2026-09-13, merge of PR #958)
* Worktree: `/home/kevin/projects/rs-studio/exp-rs-hyperspectral-spectral-intelligence-10`
* Branch: `zcode/hyperspectral-spectral-intelligence-10`
* Verified: `git rev-parse origin/master` → `7d78059d1a…` (fetch + `git pull --ff-only` clean; one transient TLS error on first pull attempt, retry clean)

## Most relevant merged PRs (spectral ownership)

| PR | Title | Takeaway for this track |
|---|---|---|
| #955 | feat(spectral): built-in redistributable spectral library and material prior API | Library domain v2 (`src/processing/algorithms/spectral_library.{h,cpp}`), `data/spectral/{library.json,library.schema.json,sensors.json,LICENSES.md}`, strict validation, Gaussian SRF `resampleTo`, material priors. **Do not redo.** |
| #954 | verification-baseline-green | READINESS.md harness at `docs/verification/`; local evidence pattern |
| #957 | whole-repo-line-review | 7 findings (F-OPS-1..5, F-PI-1/2); none in spectral scope except F-OPS-1 (class_mapping, not spectral) |
| #948/#946/#949 | lab content / grading / spec | D3 lab content identified H-1/H-2/H-3 operator gaps (ISSUES.md) — the demand side of this track |

## Historical issues / findings relevant here

* `ISSUES.md` H-1 (`rs:sam_classify` / `rs:spectral_unmixing` no `libraryPath`), H-2 (`rs:endmember_extraction` JSON-only, not pipeline-consumable), H-3 (no inverse MNF).
* `WHOLE_REPO_REVIEW.md` + `review/DEDUPE.md`: no open spectral finding; review saturation means the *delta* is where review value is.
* ADRs: 0075 (MNF), 0076 (SID), 0077 (linear unmixing), 0078 (RX), 0079 (spectral resampling), 0080 (PPI), 0081 (spectral library domain), 0082 (wavelength-aware profile), 0092 (library matching workbench), 0096 (wavelength-aware library matching), 0097 (ROI mean spectrum), 0100 (continuum removal), 0146 (library material priors).

## Capability matrix at baseline (verified by reading master)

| Capability | Kernel | Operator | Workflow-composable |
|---|---|---|---|
| SAM/SID classify | `spectral_classification.{h,cpp}` | `rs:sam_classify` (`refs` inline only) | inline arrays only |
| Linear unmixing (clip+renorm FCLS approx) | `spectral_unmixing.{h,cpp}` | `rs:spectral_unmixing` (`endmembers` inline only) | inline arrays only |
| PPI endmember extraction (streaming) | `endmember_extraction.{h,cpp}` + inline streaming copy | `rs:endmember_extraction` | **No** — terminal JSON only (H-2) |
| MNF forward (full-raster, no transform out) | `image_enhancement::mnf/processMnfFile` | `rs:mnf` | raster path only; **no inverse, no transform metadata** (H-3) |
| RX anomaly (3-pass streaming) | `spectral_anomaly.{h,cpp}` | `rs:rx_anomaly` | raster in/out |
| Matched filter / ACE (3-pass streaming) | `spectral_detection.{h,cpp}` | `rs:matched_filter`, `rs:ace` | `target` inline array only |
| Spectral resampling (linear + Gaussian SRF) | `spectral_resampling.{h,cpp}` | `rs:spectral_resample` | raster in/out, WAVELENGTH metadata propagated |
| Continuum removal | `spectral_roi`/operator-inline | `rs:continuum_removal` | raster in/out |
| Spectral derivatives | `spectral_derivative.{h,cpp}` | `rs:spectral_derivative` | raster in/out, wavelength-aware |
| Spectral library domain v2 (validate/priors/resample/match) | `spectral_library.{h,cpp}` | none (GUI workbench only) | **Not an operator input** (H-1) |
| Wavelength metadata convention | per-band GDAL `WAVELENGTH` (+`WAVELENGTH_UNITS`=nm) | read by derivative/continuum/resample/ROI; propagated by apply_mask/resample | partial |

## Gap matrix (this track)

| Gap | Evidence | Plan |
|---|---|---|
| H-2 typed structured spectral artifact | `rs_endmember_extraction_operator.cpp:358-384` (JSON payload only); `src/workflow/placeholder_grammar.cpp` resolves string ports only | WP-B |
| H-1 library as first-class operator input | `rs_sam_classify_operator.cpp` (only `refs`), `rs_spectral_unmixing_operator.cpp` (only `endmembers`) | WP-C |
| H-3 inverse MNF + transform metadata | `image_enhancement.h:20-46` (no transform matrices returned) | WP-D |
| True FCLS unmixing | operator metadata admits "approximate fully constrained" (`rs_spectral_unmixing_operator.cpp:102-104`) | WP-E |
| Bad-band mask / band exclusion by wavelength | no operator found (`ls src/operators/rs/`) | WP-F |
| MNF full-raster memory (~4x raster) | `rs_mnf_operator.cpp:52-61` (FullRaster policy declared) | WP-D streaming |

## Duplicate-development exclusions

| Do NOT redo | Because |
|---|---|
| Library parser/validator/sensors/priors | PR #955 just merged; `SpectralLibrary` namespace is the authority |
| PPI/MNF-forward/SAM/SID/RX/MF/ACE kernels | exist + tested (`tests/test_endmember_extraction.cpp`, `test_mnf.cpp`, `test_spectral_*.cpp`) |
| Linear+Gaussian SRF resampling | `SpectralResampling` (ADR 0079) |
| Placeholder grammar change | path/string placeholder is the frozen contract (#727 dual-path policy); artifacts ride on top as paths |
| Model Runtime inference | Track 08 ownership (Model Runtime 4.0) |
| SAR polarimetry/temporal ops | sibling 10.0 tracks (`zcode/advanced-sar-polsar-insar-10`, `zcode/temporal-eo-phenology-change-10`) |
| Sensor product import / SRF tables for products | `zcode/cn-eo-products-sensor-physics-10` ownership; I consume `sensors.json`/WAVELENGTH metadata only |

## Shared-file conflict watch

| File | Other tenants | Policy |
|---|---|---|
| `src/operators/rs/rs_operators_init.cpp` | any track adding operators | narrow append-only registrations |
| `tests/CMakeLists.txt`, `src/processing/algorithms/CMakeLists.txt` (or src CMake where kernels live) | all tracks | append-only entries |
| `.gitignore` | track whitelists | one 4-line block, committed first |
| `data/spectral/*` | PR #955 (merged, quiet) | append-only additions (schema extension if needed, versioned) |
| `docs/CHANGELOG.md` entries | all tracks | own section only |

## Host resource baseline

* Shared host: 16 cores / 62 GB RAM; root fs 94% used (56 GB free at baseline); `/tmp` is a 32 GB tmpfs shared with concurrent tracks (observed transient ENOSPC on /tmp at baseline — build dirs stay on /home fs; monitor `df`).
* Build: `cmake --preset build-dev` (or configure into worktree-local `build-dev/`), `-j2`, drop to `-j1` under RSS/load pressure; `CTEST_PARALLEL_LEVEL=1`; `QT_QPA_PLATFORM=offscreen`.
