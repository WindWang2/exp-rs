Test project /home/kevin/projects/rs-studio/exp-rs-classification-change-studio/build-dev
      Start 4302: test_spatial_block_leakage::Moran's I matches the closed-form lattice solution
 1/62 Test #4302: test_spatial_block_leakage::Moran's I matches the closed-form lattice solution ...............................   Passed    0.22 sec
      Start 4303: test_spatial_block_leakage::Moran's I degenerate guards return neutral zero
 2/62 Test #4303: test_spatial_block_leakage::Moran's I degenerate guards return neutral zero ..................................   Passed    0.21 sec
      Start 4304: test_spatial_block_leakage::Block partition keeps train strictly isolated from evaluation sets
 3/62 Test #4304: test_spatial_block_leakage::Block partition keeps train strictly isolated from evaluation sets ...............   Passed    0.21 sec
      Start 4305: test_spatial_block_leakage::Block partition is deterministic for a fixed seed
 4/62 Test #4305: test_spatial_block_leakage::Block partition is deterministic for a fixed seed ................................   Passed    0.21 sec
      Start 4306: test_spatial_block_leakage::Partition degenerate inputs produce empty reports
 5/62 Test #4306: test_spatial_block_leakage::Partition degenerate inputs produce empty reports ................................   Passed    0.20 sec
      Start 4307: test_glcm_texture::GLCM deg0 on ideal stripes hits hand-derived metrics
 6/62 Test #4307: test_glcm_texture::GLCM deg0 on ideal stripes hits hand-derived metrics ......................................   Passed    0.21 sec
      Start 4308: test_glcm_texture::GLCM deg90 on ideal stripes is perfectly self-similar
 7/62 Test #4308: test_glcm_texture::GLCM deg90 on ideal stripes is perfectly self-similar .....................................   Passed    0.21 sec
      Start 4309: test_glcm_texture::GLCM omnidirectional stripes average to closed-form metrics
 8/62 Test #4309: test_glcm_texture::GLCM omnidirectional stripes average to closed-form metrics ...............................   Passed    0.21 sec
      Start 4310: test_glcm_texture::GLCM constant window saturates to the ordered extreme
 9/62 Test #4310: test_glcm_texture::GLCM constant window saturates to the ordered extreme .....................................   Passed    0.21 sec
      Start 4311: test_glcm_texture::GLCM quantization clamps the top bin
10/62 Test #4311: test_glcm_texture::GLCM quantization clamps the top bin ......................................................   Passed    0.21 sec
      Start 4312: test_glcm_texture::GLCM normalized co-occurrence conserves probability mass
11/62 Test #4312: test_glcm_texture::GLCM normalized co-occurrence conserves probability mass ..................................   Passed    0.19 sec
      Start 4313: test_glcm_texture::GLCM non-finite or empty windows yield NaN sentinels
12/62 Test #4313: test_glcm_texture::GLCM non-finite or empty windows yield NaN sentinels ......................................   Passed    0.20 sec
      Start 4314: test_glcm_texture::GLCM feature map is exact in interiors and NaN-propagating
13/62 Test #4314: test_glcm_texture::GLCM feature map is exact in interiors and NaN-propagating ................................   Passed    0.20 sec
      Start 4315: test_classifier_engine::Engine factory and untrained-model guards
14/62 Test #4315: test_classifier_engine::Engine factory and untrained-model guards ............................................   Passed    0.20 sec
      Start 4316: test_classifier_engine::KMeans recovers the three analytic distribution centres
15/62 Test #4316: test_classifier_engine::KMeans recovers the three analytic distribution centres ..............................   Passed    0.20 sec
      Start 4317: test_classifier_engine::Random forest reaches the separability floor with conserved posteriors
16/62 Test #4317: test_classifier_engine::Random forest reaches the separability floor with conserved posteriors ...............   Passed    0.24 sec
      Start 4318: test_classifier_engine::NormalBayes reaches the separability floor with true posteriors
17/62 Test #4318: test_classifier_engine::NormalBayes reaches the separability floor with true posteriors ......................   Passed    0.22 sec
      Start 4319: test_classifier_engine::RBF SVM separates the three blobs and predicts 1-hot
18/62 Test #4319: test_classifier_engine::RBF SVM separates the three blobs and predicts 1-hot .................................   Passed    0.21 sec
      Start 4320: test_classifier_engine::ISODATA splits a fused bimodal cluster once
19/62 Test #4320: test_classifier_engine::ISODATA splits a fused bimodal cluster once ..........................................   Passed    0.22 sec
      Start 4321: test_classifier_engine::NormalBayes posterior matches the closed form for correlated covariances
20/62 Test #4321: test_classifier_engine::NormalBayes posterior matches the closed form for correlated covariances .............   Passed    0.20 sec
      Start 4322: test_classifier_engine::NormalBayes ridge survives a singular class covariance
21/62 Test #4322: test_classifier_engine::NormalBayes ridge survives a singular class covariance ...............................   Passed    0.20 sec
      Start 4323: test_classification_postprocess::Sieve removes salt pixels but never erodes the valid core
22/62 Test #4323: test_classification_postprocess::Sieve removes salt pixels but never erodes the valid core ...................   Passed    0.20 sec
      Start 4324: test_classification_postprocess::Sieve honours connectivity on diagonal chains
23/62 Test #4324: test_classification_postprocess::Sieve honours connectivity on diagonal chains ...............................   Passed    0.20 sec
      Start 4325: test_classification_postprocess::Sieve reassigns to the neighbour with the longest shared border
24/62 Test #4325: test_classification_postprocess::Sieve reassigns to the neighbour with the longest shared border .............   Passed    0.21 sec
      Start 4326: test_classification_postprocess::Sieve hands pixels to NoData when no thematic neighbour exists
25/62 Test #4326: test_classification_postprocess::Sieve hands pixels to NoData when no thematic neighbour exists ..............   Passed    0.21 sec
      Start 4327: test_classification_postprocess::Majority filter removes isolated peaks and keeps ties
26/62 Test #4327: test_classification_postprocess::Majority filter removes isolated peaks and keeps ties .......................   Passed    0.20 sec
      Start 4328: test_classification_postprocess::Majority filter ignores NoData neighbours and preserves NoData centres
27/62 Test #4328: test_classification_postprocess::Majority filter ignores NoData neighbours and preserves NoData centres ......   Passed    0.20 sec
      Start 4329: test_classification_postprocess::Clump-and-eliminate composite matches the sieve guarantee
28/62 Test #4329: test_classification_postprocess::Clump-and-eliminate composite matches the sieve guarantee ...................   Passed    0.21 sec
      Start 4330: test_classification_postprocess::Degenerate inputs round-trip or return empty
29/62 Test #4330: test_classification_postprocess::Degenerate inputs round-trip or return empty ................................   Passed    0.23 sec
      Start 4331: test_change_detector::Difference and normalized difference follow algebraic identities
30/62 Test #4331: test_change_detector::Difference and normalized difference follow algebraic identities .......................   Passed    0.22 sec
      Start 4332: test_change_detector::Log ratio is exactly zero for identical dates and matches the hand-expanded constant
31/62 Test #4332: test_change_detector::Log ratio is exactly zero for identical dates and matches the hand-expanded constant ...   Passed    0.22 sec
      Start 4333: test_change_detector::CVA magnitude and direction recover the 3-4-5 triangle
32/62 Test #4333: test_change_detector::CVA magnitude and direction recover the 3-4-5 triangle .................................   Passed    0.22 sec
      Start 4334: test_change_detector::CVA direction angle hits exact axis branches
33/62 Test #4334: test_change_detector::CVA direction angle hits exact axis branches ...........................................   Passed    0.22 sec
      Start 4335: test_change_detector::CVA adaptive threshold separates a calibrated change fraction
34/62 Test #4335: test_change_detector::CVA adaptive threshold separates a calibrated change fraction ..........................   Passed    0.22 sec
      Start 4336: test_change_detector::PCA difference projects onto the analytic minor axis
35/62 Test #4336: test_change_detector::PCA difference projects onto the analytic minor axis ...................................   Passed    0.21 sec
      Start 4337: test_confusion_matrix::Perfect diagonal gives OA = kappa = 1
36/62 Test #4337: test_confusion_matrix::Perfect diagonal gives OA = kappa = 1 .................................................   Passed    0.20 sec
      Start 4338: test_confusion_matrix::Canonical 3x3 matrix reproduces every hand-computed metric
37/62 Test #4338: test_confusion_matrix::Canonical 3x3 matrix reproduces every hand-computed metric ............................   Passed    0.21 sec
      Start 4339: test_confusion_matrix::Streaming tile accumulation is bit-identical to single-shot
38/62 Test #4339: test_confusion_matrix::Streaming tile accumulation is bit-identical to single-shot ...........................   Passed    0.21 sec
      Start 4340: test_confusion_matrix::Off-target classes are excluded and zero-presence classes degrade to zero
39/62 Test #4340: test_confusion_matrix::Off-target classes are excluded and zero-presence classes degrade to zero .............   Passed    0.21 sec
      Start 4341: test_confusion_matrix::Degenerate agreement cases keep kappa finite
40/62 Test #4341: test_confusion_matrix::Degenerate agreement cases keep kappa finite ..........................................   Passed    0.23 sec
      Start 4342: test_classification_agent_tools::JM distance vanishes for identical distributions
41/62 Test #4342: test_classification_agent_tools::JM distance vanishes for identical distributions ............................   Passed    0.22 sec
      Start 4343: test_classification_agent_tools::JM distance saturates at 2 for disjoint Gaussians
42/62 Test #4343: test_classification_agent_tools::JM distance saturates at 2 for disjoint Gaussians ...........................   Passed    0.20 sec
      Start 4344: test_classification_agent_tools::JM distance matches the closed form for shifted equal variances
43/62 Test #4344: test_classification_agent_tools::JM distance matches the closed form for shifted equal variances .............   Passed    0.20 sec
      Start 4345: test_classification_agent_tools::JM distance handles unequal variances via the analytic reduction
44/62 Test #4345: test_classification_agent_tools::JM distance handles unequal variances via the analytic reduction ............   Passed    0.20 sec
      Start 4346: test_classification_agent_tools::Diagnosis flags spectral confusion on a weak kappa
45/62 Test #4346: test_classification_agent_tools::Diagnosis flags spectral confusion on a weak kappa ..........................   Passed    0.20 sec
      Start 4347: test_classification_agent_tools::Diagnosis reports per-class JM and stays OK for separable classes
46/62 Test #4347: test_classification_agent_tools::Diagnosis reports per-class JM and stays OK for separable classes ...........   Passed    0.20 sec
      Start 4348: test_classification_agent_tools::Diagnosis recommends pruning redundant low-gain features
47/62 Test #4348: test_classification_agent_tools::Diagnosis recommends pruning redundant low-gain features ....................   Passed    0.23 sec
      Start 4349: test_classification_agent_tools::Diagnosis rejects malformed input with an ERROR status
48/62 Test #4349: test_classification_agent_tools::Diagnosis rejects malformed input with an ERROR status ......................   Passed    0.19 sec
      Start 4350: test_magic_wand_roi::Magic wand extracts the disk with area-grade fidelity and zero leaks
49/62 Test #4350: test_magic_wand_roi::Magic wand extracts the disk with area-grade fidelity and zero leaks ....................   Passed    0.31 sec
      Start 4351: test_magic_wand_roi::Magic wand respects the pixel budget and rejects invalid seeds
50/62 Test #4351: test_magic_wand_roi::Magic wand respects the pixel budget and rejects invalid seeds ..........................   Passed    0.27 sec
      Start 4352: test_magic_wand_roi::Magic wand blocks on any band difference (no spectral leaks)
51/62 Test #4352: test_magic_wand_roi::Magic wand blocks on any band difference (no spectral leaks) ............................   Passed    0.27 sec
      Start 4353: test_feature_scatter::Density thumbnail bins hand-placed points per the documented formula
52/62 Test #4353: test_feature_scatter::Density thumbnail bins hand-placed points per the documented formula ...................   Passed    0.17 sec
      Start 4354: test_feature_scatter::Density rendering survives degenerate single-valued clouds
53/62 Test #4354: test_feature_scatter::Density rendering survives degenerate single-valued clouds .............................   Passed    0.18 sec
      Start 4355: test_feature_scatter::100k samples bin and render within the 40 ms budget
54/62 Test #4355: test_feature_scatter::100k samples bin and render within the 40 ms budget ....................................   Passed    0.18 sec
      Start 4356: test_classification_studio_widget::Studio palette table reflects the class definition
55/62 Test #4356: test_classification_studio_widget::Studio palette table reflects the class definition ........................   Passed    0.28 sec
      Start 4357: test_classification_studio_widget::Swipe slider and algorithm combo emit typed signals
56/62 Test #4357: test_classification_studio_widget::Swipe slider and algorithm combo emit typed signals .......................   Passed    0.28 sec
      Start 4358: test_classification_studio_widget::Studio magic wand emits the extracted ROI with the selected class
57/62 Test #4358: test_classification_studio_widget::Studio magic wand emits the extracted ROI with the selected class .........   Passed    0.31 sec
      Start 4359: test_classification_studio_widget::Studio wand is QPointer-safe against destroyed layers
58/62 Test #4359: test_classification_studio_widget::Studio wand is QPointer-safe against destroyed layers .....................   Passed    0.30 sec
      Start 4360: test_d15_classification_change_e2e::D15 full pipeline clears the OA / kappa / Dice gates
59/62 Test #4360: test_d15_classification_change_e2e::D15 full pipeline clears the OA / kappa / Dice gates .....................   Passed    9.04 sec
      Start 4361: test_d15_classification_change_e2e::Lab03 landcover contract grades the classified artifact at 100
60/62 Test #4361: test_d15_classification_change_e2e::Lab03 landcover contract grades the classified artifact at 100 ...........   Passed    0.39 sec
      Start 4362: test_d15_classification_change_e2e::Lab04 change contract grades the CVA artifact at 100
61/62 Test #4362: test_d15_classification_change_e2e::Lab04 change contract grades the CVA artifact at 100 .....................   Passed    0.23 sec
      Start 4363: test_d15_classification_change_e2e::Lab03 / 04 / 11 specs stay loadable with intact grading refs
62/62 Test #4363: test_d15_classification_change_e2e::Lab03 / 04 / 11 specs stay loadable with intact grading refs .............   Passed    0.20 sec

100% tests passed out of 62

Total Test time (real) =  22.63 sec

---

# EVIDENCE — D15 Classification & Change Detection Studio (final verification)

Date: 2026-09-15 (worktree session 2026-09-14 23:00 → 2026-09-15)
Branch: `zcode/classification-change-studio`
Worktree: `/home/kevin/projects/rs-studio/exp-rs-classification-change-studio`
Baseline: `origin/master` @ `007e70cff6f43151aef6cf7e501c14bcb94a5090`

## Completion gate

- [x] Worktree isolation: all development/commits in the worktree; the master
  checkout was never modified or built in (`git -C main status` clean throughout).
- [x] Build & offline tests 100% green:
  - D15 precise suite: **62/62 passed** (11 binaries, 22.9 s wall, `-j1`, offscreen)
  - Spec gate regex `test_spatial_block|test_glcm|test_classifier|test_classification|test_change|test_confusion|test_d15`:
    **56/56 passed** (20.9 s wall)
  - The log above is the raw final suite run (stdout of the gate command).
- [x] Memory safety: one real defect (iterator invalidation → SIGSEGV in the
  outline walk) was caught by the tests and fixed under gdb during the
  Package G loop; a dedicated sanitizer lane was NOT exercised in this run
  (dev-default preset has ENABLE_SANITIZERS=OFF, matching the main checkout;
  a second full -j2 sanitizer build did not fit the envelope) — recorded as
  the run's known limitation.  valgrind is not installed on this host.
- [x] Zero tautology / zero private access: all expected values are hand-derived
  closed forms or committed fixtures; no `friend`, no `#define private public`;
  tests use public API only (verified independently by both review axes, which
  recomputed the Moran/GLCM/confusion closed forms from scratch).
- [x] Dual-axis review: 2 read-only subagents; Axis 1 78→post-fix, Axis 2 88;
  P0 = 0, P1 = 0 after fixes (all findings + resolutions in REVIEW_LOG.md).
- [x] Lab contracts: lab03 landcover and lab04 change artifacts graded at
  score **100** through the real `OutputVerifier` + committed rules;
  lab03/04/11 specs validated loadable with intact grading references.
- [x] Remote CI: none (ci=none). Nothing pushed.

## Red → green evidence per package

| Pkg | Test binary | RED (stub) | GREEN |
|---|---|---|---|
| A | test_spatial_block_leakage | 3/5 failed | 5/5 |
| B | test_glcm_texture | 8/8 failed | 8/8 |
| C | test_classifier_engine | 6/7 failed | 7/7 |
| D | test_classification_postprocess | 7/8 failed | 8/8 |
| E | test_change_detector | 6/6 failed | 6/6 |
| F | test_confusion_matrix | 5/5 failed | 5/5 |
| G | test_magic_wand_roi + test_feature_scatter + test_classification_studio_widget | 8/10 failed (incl. 3 SEGFAULT) | 10/10 |
| H | test_classification_agent_tools | 6/8 failed | 8/8 |
| I | test_d15_classification_change_e2e | — (test-side pipeline; defects found and fixed in-run: BSQ scene synthesis, CVA α calibration) | 4/4 |

Commits: `4eaab30f01` (phase 0) → `a749adc7c9` (A) → `4bef4929dd` (B) →
C, D, E, F, H, G, I (atomic per package) → review-fix commit (this one).

## Pre-existing failures at HEAD (NOT introduced by D15)

- `tests/test_io_operators.cpp` — `OSRImportFromWkt(const_cast<char**>(...), &srs)`
  argument order does not compile against system GDAL 3.13
  (`error: cannot convert ‘char**’ to ‘OGRSpatialReferenceH’`), 36 errors.
- `tests/test_detection_nms_10.cpp` — 84 compile errors at HEAD.

Both files are untouched by this track (`git diff 4eaab30f01..HEAD` lists only
the additive D15 files, the three CMake source-list entries, and the additive
TEST_PREFIX plumbing in tests/CMakeLists.txt); they fail identically on a
pristine baseline checkout.  They block nothing: no D15 target depends on them.

## Environment notes

- `/home/kevin/.local/bin/{cmake,ctest}` are broken shims (they launch an
  unrelated GUI app); absolute paths `/usr/bin/cmake`, `/usr/bin/ninja`,
  `/usr/bin/ctest` used everywhere.
- Build: `ninja -C build-dev -j2` throughout, `QT_QPA_PLATFORM=offscreen`,
  `ctest -j1`; peak RSS ≈ 35% of 62 GiB (the 70% breaker never fired).
- Full-build note: the first full build stopped at the pre-existing
  test_io_operators failure (5724/6259); `ninja -k 0` completed the rest —
  final state: everything builds except the two pre-existing broken TUs above.
