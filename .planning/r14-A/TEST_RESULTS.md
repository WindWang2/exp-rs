# R14-A Test Results — real runs, real exit codes

Host: linux x64, GCC, Debug build, worktree-local `build-r14-A`.
Env: `QT_QPA_PLATFORM=offscreen`, `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib` (CTest
injects the same via CTestCustom; direct binary runs set it explicitly), all builds/tests under
`flock /tmp/exp-rs-r14-global-build.lock`, builds `-j2`, tests serial. Catch2 v3.7.1.

## Build

- `cmake --build build-r14-A --parallel 2 --target <11 test targets>` → `[100%] Built target
  test_classifier_kmeans`, overall exit 0 (after queueing behind tracks B/C on the shared lock).
- Pre-flight per-file compile check caught 3 errors before they hit the queue (committed as
  9133e9d52): nBands scope in the staged CSV writer; const buffer → GDALRasterIO void*;
  bandNoDataValue takes bool*.

## RED → GREEN×2 matrix (direct Catch2 runs, rc: 0 = pass)

| WP | Test (binary, filter) | RED (pre-fix code) | GREEN #1 | GREEN #2 |
|---|---|---|---|---|
| A1 temporal probe | test_temporal_algorithms `*EVI probe ignores undeclared*` | rc=42 (assert fail) | rc=0 (17 assert) | rc=0 |
| A2 local RX diagonal | test_spectral_local_rx `*Diagonal-mode zero-variance*` | rc=42 | rc=0 (518 assert) | rc=0 |
| A3 sam hetero NoData | test_spectral_classification `*heterogeneous per-band NoData*` (sam case) | rc=42, sam case failed (1/2) | rc=0 | rc=0 (43 assert / 2 cases) |
| A4 continuum hetero | same binary (continuum case) | rc=42, continuum case failed (1/2) | rc=0 | rc=0 |
| A5 qa NoData | test_radiometric_qa `*treats declared NoData as invalid*` | rc=42 | rc=0 (11 assert) | rc=0 |
| A6 fusion hetero | test_image_fusion `*heterogeneous per-band NoData*` | rc=42 | rc=0 (21 assert) | rc=0 |
| A6 #700 update | test_image_fusion `*#700*` | (convention change, see ORACLES) | rc=0 | rc=0 |
| A7a csv publish | test_obia_operators `*republishes the CSV*` | n/a — success-path contract pin (no fault injection; atomicity is structural: writeFileAtomic stage+fsync+publish) | rc=0 (11 assert) | rc=0 |
| A7b kmeans determinism | test_classifier_kmeans `*bit-identical centers*` | rc=42 with unseeded cv::kmeans (observed this run; inherently probabilistic pre-fix) | rc=0 (312 assert) | rc=0 |

RED method: `git show origin/master:<production file> > <file>` → incremental rebuild of the
affected test target → run → confirm failure → `git checkout HEAD -- <file>` → rebuild.
A3/A4 RED re-verified with a filter proven to match (first attempt's filter matched nothing —
rc=2 — and was discarded as invalid evidence).

## Full regression — FINAL state (all fixes in), all rc=0

- test_radiometric_vocabulary_contract — 72 assertions / 5 cases (baseline #1450 dedup verification)
- test_band_math — 130,277 assertions / 49 cases
- test_spectral_local_rx — 567 / 7
- test_radiometric_qa — 86 / 9
- test_classifier_kmeans — 2,122 / 3
- test_image_fusion — 1,439 / 31
- test_spectral_classification — 176 / 29
- test_temporal_algorithms — 1,202 / 44
- test_obia_operators — 170 / 18
- test_e2e_science_numerics — 281 / 20
- test_rs_operators — 9,066 / 81

One intermediate regression found and closed during verification: the unconditional continuum
NaN tag broke #473's absent-NoData pin in test_rs_operators (1/9066 failed); fixed with the
conditional tag (commit "writes the NaN void tag conditionally"); test_rs_operators then fully
green twice.

Test-fixture correction found during verification: GTiff stores ONE NoData tag per dataset, so
heterogeneous per-band fixtures must ride VRT wrappers (same strategy as test_r5_nodata_contracts);
fixtures fixed + sanity-checked (commit f7a236b53).

## Not run / out of scope (honest disclosure)

- No performance benchmark runs this round: the fixes are correctness/durability work; no
  throughput claim is made anywhere in the PR. (WP12 profiling deferred — nothing in this diff
  changes hot loops except the washes, which are O(pixels) exact compares.)
- ctest as a whole suite not run (1010 test binaries); the 11 binaries covering every touched
  code path + the science baseline targets were run directly. CTest discovery of the 11 targets
  exists (built via catch_discover_tests); direct invocation is equivalent evidence per binary.

## Final-commit verification (post-review round 2, HEAD = docs-only delta over the tested state)

All r14a cases + continuum #473 + hetero continuum re-run at final HEAD: all rc=0
(per-review-protocol closing run; code files identical to the fully-regressed state — only
.planning docs landed after).
