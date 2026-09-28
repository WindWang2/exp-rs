# R5 Track 02 — RS/GDAL fused execution, operator correctness, NoData/buffer lifecycle closure

Fixes #1356

## Baseline & sync

- Execution-time BASE_SHA: `a726d17a6224632d929e782e996351732632f272`
  (origin/master, re-fetched before any change; identical to the planning snapshot).
- Final sync target: same commit (master did not move during the run; re-verified
  before opening the PR).

## Read inputs

- Issue #1356 (seed; OPEN, 0 comments at start) — closed here.
- PR #1340 (R4 operator-correctness audit — the deferred D4/D4b rows this track closes).
- PRs #1347/#1354 (perf-memory + CI red-zone context), #1338 (io/processing closure —
  its io: warp call sites are deliberately untouched).
- Open sibling PRs #1365/#1366 checked for overlap: none with this branch's files.

## 1. #1356 root cause — test harness UAF, not GDAL, not the fused executor

The stable SEGFAULT was a use-after-scope in `tests/test_fused_chain.cpp`: the fixture
built `QCoreApplication(argc, argv)` from **stack locals**. QCoreApplication stores the
`int&`/`char**` by pointer and never copies, so the later `applicationFilePath()`
dereferenced dead stack. gdb backtrace (deterministic):

```
ChangeDetection::changeMask (SICNU_LOG_INFO)
  -> QgsMessageLog::logMessage -> QgsApplication::members()  [lazy ApplicationMembers]
  -> QgsLocalizedDataPathRegistry -> QgsSettings -> QLibraryInfo::path
  -> QCoreApplication::applicationFilePath() -> SIGSEGV
```

It fired inside the *unfused reference chain* of the equivalence case — the fused
executor never ran in the crashing builds. Verdict against the issue's fork: neither a
GDAL 3.10/3.13 regression nor a fused-chain UAF. Fix = the repo's own convention
(`static argc/argv`, as every other suite uses); the same latent hazard in
`test_data_manager_reap.cpp` (static argc, local argv array) is fixed too.

GDAL version-matrix evidence: this host has exactly one GDAL (3.13.3, user-local SDK
root) and no container runtime; since the cause is proven repo-side UB, a version
matrix cannot change the outcome — the guard is the static-storage pattern plus the
permanently green equivalence case.

## 2. Fused tail parity (payload + metadata)

The fused threshold output silently lacked what the unfused operator writes:
`maskedPixels`/`totalPixels`/`maskedPercent` and the `SICNU_CHANGE_METHOD` /
`SICNU_CHANGE_THRESHOLD` metadata. `FusedStage` gains an output-metadata list, a
tail-plane observer (consumer thread) and a post-run extras carrier; the equivalence
test now asserts payload-count and metadata equality fused vs unfused, extending
bit-identical beyond pixels (values, NoData declaration, geotransform, CRS, manifest).

## 3. R5 NoData audit fixes (adjacent contracts, existing operators only)

| fix | file(s) |
|---|---|
| filter/speckle: declared sentinels NaN-washed per band before window kernels (optional `noData` on `streamBandWindowed`; undeclared bands unchanged) | image_enhancement_streaming.{h,cpp}, rs_image_enhancement_operator.cpp |
| rs:sar_coregister (global) declares its NaN resampling voids (mirror of the local variant) | rs_sar_coregister_operator.cpp |
| rs:sar_interferogram declares NaN on the interferogram band (coherence parity) | rs_sar_interferogram_operator.cpp |
| WarpOptions optional src/dst nodata lists; rs:resample/rs:align pin declarations (declared-everywhere → pin; undeclared float → `-dstnodata nan`; integer/mixed → GDAL per-band default) | raster_convert.{h,cpp}, rs_grid_operators.cpp |
| rs:register_images honors the source's declared sentinel in the warp and declares it (closes the R4 "tracked as backlog" note) | rs_register_images_operator.cpp |
| MODIS georeference keeps declarations through the sinusoidal copy; warpToCrs pins explicit nodata | satellite_products.cpp |
| rs:obia_segment (simple) / rs:obia_classify (quantize): per-band sentinel wash instead of band-1-only (#803 family) | rs_obia_segment_operator.cpp, rs_obia_classify_operator.cpp |
| rs:spectral_similarity: per-band sentinels all honored (heterogeneous declarations; undeclared bands keep the documented −9999 fallback) | rs_spectral_similarity_operator.cpp |
| RsOtbSegmenter::relabelAllVoidSegments — all-void segments relabel to 0 (ADR 0054); mixed segments stay (features layer masks per-pixel already) | rs_otb_segmenter.{h,cpp} |

Deliberately untouched: io: operators' `warpRaster` call sites (io surface belongs to
#1338's closure; the new WarpOptions fields default to the old behavior).

## 4. Master build break fixed (cross-track note)

`tests/CMakeLists.txt` carried an unresolved conflict marker from merge `7bb6398c0`
(stray `=======` + glued comment): every fresh configure with `ENABLE_TESTS=ON`
failed at parse. Fixed as a 2-hunk restore verified against both parents. All
parallel R5 tracks hitting this need the same restore.

## 5. Tests

New: `tests/test_r5_nodata_contracts.cpp` (`ctest -R "^r5::"`, 8 cases / 376
assertions) — sentinel≡NaN bit-equivalence for 5 window kernels, warp void
declarations (resample/align), register source-sentinel contract, SAR NaN
declarations, heterogeneous per-band sentinels (similarity via VRT — GTiff keeps one
nodata tag per dataset — and OBIA), OTB void-relabel oracle
(in test_obia_operators), MODIS declaration survival (in test_satellite_products).

Full local Debug runs (QT_QPA_PLATFORM=offscreen, -j2, GDAL 3.13.3 / GCC 16.2.1 /
Qt 6.11.2): every touched suite green —

```
test_fused_chain 66/4            test_r5_nodata_contracts 376/8
test_image_enhancement 6220/13   test_speckle_filter 15869/26
test_registration_operators 37/4 test_grid_operators 75/5
test_resampler 283/9             test_sar_coregistration 20555/4
test_sar_platform10 2766/11      test_sar_platform11 351/5
test_operator_nodata_semantics 254/7   test_known_answer_corpus_r4 269/8
test_algorithm_meta_drift 7761/1 test_obia_operators 159/17
test_satellite_products 498/21   test_data_manager_reap 134/14
```

Sanitizer: scoped ASan+UBSan build (repo `sanitizer-debug` preset) of the crash-path
test — see the ASan section appended below. (valgrind was tried first: no system
valgrind and the CachyOS ld.so is stripped with no matching debug package, so the
user-local valgrind cannot start memcheck.)

## 6. Compatibility & risk

- Declarations-only fixes (SAR, register output) add metadata; no pixel changes.
- Pixel-visible changes, each covered by a regression: filter/speckle sentinel
  centers now write NaN (matching the already-declared NaN); resample/align of
  undeclared float rasters now declare NaN voids (previously undeclared zeros);
  similarity/OBIA honor per-band declarations (previously voids could score as
  data). Downstream NaN-aware readers (GdalDatasetWrapper conditioning) already
  normalize non-finite values.
- Performance: the pre-wash runs only for bands with a declared finite sentinel and
  is an O(tile) pass replacing nothing on the hot path; warp contract adds two argv
  strings to one GDALWarp call. Debug fused-equivalence timing unchanged.

## 7. Independent review

A fresh-eyes reviewer subagent audited `origin/master...HEAD` against the issue,
this track's brief and the test evidence: **PASS-with-issues (0 Blocker)**.
All High findings were fixed before push:

- the remaining stack-local argc/argv sites (test_gui_job_adapter.cpp ×3,
  test_execution_plane.cpp ×1) joined the #1356 harness fix, with a complete
  family-sweep count in the commit message;
- warpRaster's nodata serialization switched to locale-independent,
  round-trip-exact `std::to_chars` (a %-format could corrupt fractional
  sentinels into mask mismatches);
- mixed-declaration float rasters no longer get a blanket NaN declaration
  (policy aligned with warpToCrs);
- commit history reordered so every test lands with/after its fix (HEAD tree
  verified identical to the reviewed tree);
- new pin regression: undeclared-float `rs:align` pads NaN + declares NaN;
- single-use plan contract documented; relabel skips the label rewrite when
  no segment is dead; comment accuracy fixes.

Full disposition list: `.planning/r5-rs-gdal-fused-correctness/DECISIONS.md` (D7).

## 8. Out of scope (flagged for owners)

- io: warpRaster call sites keep GDAL-default nodata behavior (io surface = #1338).
- #1355 (GDAL_DRIVER_PATH pin) → Track 05; #1357/#1358 (other suites' teardown
  races) → their tracks.

Remote CI was not awaited per execution contract.
