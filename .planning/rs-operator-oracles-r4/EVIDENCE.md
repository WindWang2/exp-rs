# EVIDENCE.md — Track 7 (R4) verification evidence

All verification on the isolated worktree `hardening/r4-operator-oracles`,
fresh build dir `build-r4/` (Debug, ENABLE_TESTS=ON), Linux/GCC 16.2.1,
ninja 1.12.1 capped at `-j2` (brief resource red-line), `QT_QPA_PLATFORM=
offscreen`, `CTEST_PARALLEL_LEVEL=1`.

## 1. Baseline evidence (Phase 0)

- `git rev-parse origin/master` after fetch → `15e5c66b5…` (unchanged since
  the brief; verified again before PR).
- Count anchors: 128 cpp / 131 h / 158 registrations / 151 classes / 111
  analysis files / 85-of-128 cpp mentioning nodata (commands + outputs in
  BASELINE.md §3).
- Open PRs #1334–#1338 file lists checked against the whitelist; only
  textual overlap: `tests/CMakeLists.txt` (append-only) with #1334/#1335.

## 2. Audit evidence

Three read-only sweeps (subagent transcripts, token-metered in the ledger)
produced file:line citations per operator; consolidated into
`.planning/rs-operator-oracles-r4/NODATA_SEMANTIC_MATRIX.md` (115 rows).
Every claimed defect was re-verified by direct code read before being
fixed; the rs:pca claim was REFUTED that way and the matrix row corrected
(commit history documents the correction).

## 3. Defect-fix commits (each independently compilable)

| commit | defect | potency test |
|---|---|---|
| be1b0f8ee | rs:spectral_derivative sentinel-as-data | nodata_semantics "spectral derivative treats declared sentinels like NaN" |
| b6225cf35 | rs:spectral_similarity hardcoded sentinel | corpus_r4 "labels … honours the declared sentinel" |
| bbf5e7ee0 | rs:image_enhancement ratio sentinel + undeclared outputs | nodata_semantics ratio + stretch cases |
| c6e90edea | band_tools IHS/stretch undeclared outputs | nodata_semantics stretch declaration case |
| f4a19336e | rs:sar_phase_filter undeclared NaN | declaration read-back (build + registry surface) |
| (register_images) | rs:register_images undeclared warp voids | declaration read-back via output GDAL metadata |
| b1e33b126 | rs:sar_polsar_decompose sentinel-as-data | polsar exclusion via noDataSamples counter (registry surface) |

## 4. Test-run evidence (measured 2026-09-27)

- Build: `ninja -j2 sicnu_processing` → **exit 0** (1447/1447 steps).
- Build: five new light-lane test targets → **exit 0**.
- Suite results (direct binary runs and ctest agree):
  | suite | cases | assertions |
  |---|---|---|
  | test_operator_nodata_semantics | 7 | 254 |
  | test_operator_chain_tolerance | 3 | 504 |
  | test_operator_preflight_refusals | 5 | 86 |
  | test_known_answer_corpus_r4 | 8 | 269 |
  | test_operator_determinism_digest | 12 | 129 |
  | **total (post-review)** | **35** | **1,242** |
  (pre-review: 33 cases / 1,211 assertions)
- Potency (red) demonstration: with `src/` reverted to 8d6cc2fcc
  (pre-fix), rebuilt and re-run: nodata_semantics → **3 cases red**
  (derivative NaN-ization, ratio masking, stretch declaration — exactly
  the defect regressions); corpus_r4 similarity case initially still
  green (a negative sentinel is caught by the kernel's reflectance-like
  negativity guard, and −9999 collides with the hardcoded fallback) →
  fixture corrected to declared sentinel **+255**, re-demonstrated
  **red pre-fix**; fixes restored → all green.
- Gate double run (catch_discover_tests TEST_PREFIX `r4::`, D15
  convention):
  pre-review:  `ctest -R "^r4::" -j1` → 33/33 passed, exit 0 (12.92 s) ×2
  post-review: `ctest -R "^r4::" -j1` → **35/35 passed, exit 0** (15.56 s)
               `ctest -R "^r4::" -j1` → **35/35 passed, exit 0** (13.18 s)
- Scope note (honest): the brief's full regex `operator|nodata|known|
  determinis` spans ~all 194 test targets; this worktree builds only the
  sicnu_processing/sicnu_operators closure (a full qgis_gui-chain build
  at -j2 is out of proportion to the touched surface). The gate regex is
  narrowed to `^r4::` (all five new suites); touched seams are covered
  by the R4 suites themselves. No pre-existing binary is available in
  this partial tree for neighbor re-runs; fixes are declaration-only or
  narrowly scoped per the matrix rows.

## 5. Known non-goals / residuals

- Warp-family nodata contract (rs:resample / rs:align /
  rs:modis_georeference), SAR coregister resampling contract,
  interferogram CFloat32 declaration, OBIA OTB-engine sentinel contract →
  documented backlog (DECISIONS D4).
- `tests/CMakeLists.txt` overlaps in-flight PRs #1334/#1335 (append-only
  tail; rebase expectation recorded in the PR body).
