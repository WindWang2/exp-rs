# R5 Track 02 — Evidence log

Machine: CachyOS, GCC 16.2.1, Qt 6.11.2, GDAL 3.13.3 (user-local SDK root
`/home/kevin/pwb-sdks/root/usr`), Debug builds, `-j2` throughout.

## 1. #1356 reproduction (pre-fix)

- Binary: Debug `tests/test_fused_chain`, run `[fused_chain][equivalence]`.
- Result: SIGSEGV every run (deterministic), Catch2 reports
  "fatal error condition: SIGSEGV" after 7 passed assertions.
- gdb backtrace (key frames):
  ```
  #0  libc (wild read)
  #1  Qt6Core -> QCoreApplication::applicationFilePath()
  #11 QSettings::QSettings
  #13 QgsSettings::QgsSettings (src/core/settings/qgssettings.cpp:81)
  #19 QgsSettings::get (qgssettings.cpp:355)
  #23 QgsLocalizedDataPathRegistry::readFromSettings (…:113)
  #26 QgsApplication::ApplicationMembers::ApplicationMembers (qgsapplication.cpp:2777)
  #27 QgsApplication::members (qgsapplication.cpp:3066)
  #28 QgsApplication::messageLog (qgsapplication.cpp:2689)
  #29 QgsMessageLog::logMessage (qgsmessagelog.cpp:56)
  #30 ChangeDetection::changeMask (change_detection.cpp:60)   <- SICNU_LOG_INFO
  #31 writeMaskFromMagnitude (rs_change_streaming.cpp:305)
  #32 thresholdRasterToMask (rs_change_streaming.cpp:1338)
  #33 RsThresholdRasterOperator::run
  #34 runOperator (test_fused_chain.cpp:117)                  <- UNFUSED reference chain
  ```
- Root cause: fixture built `QCoreApplication(argc, argv)` from stack locals;
  Qt stores the `int&`/`char**` by pointer → dead-stack dereference in
  `applicationFilePath()`. Fused executor never ran in the crashing runs.

## 2. #1356 post-fix

- `./tests/test_fused_chain "[fused_chain][equivalence]"` →
  **All tests passed (40 assertions)**; full binary →
  **All tests passed (66 assertions in 4 test cases)**.
- Repeated runs stable (seeded randomness varies; result identical).

## 3. R5 regression suite (new, `ctest -R "^r5::"`)

`tests/test_r5_nodata_contracts.cpp`: **8 cases / 376 assertions, all pass** —
covers enhancement window sentinel≈NaN equivalence (bit-for-bit, 5 kernels),
resample/align void declarations, register_images source-sentinel contract,
SAR coregister + interferogram NaN declarations, similarity heterogeneous
per-band sentinels (VRT fixture), OBIA simple per-band sentinels (VRT fixture).

## 4. Module regressions (all green, same Debug build)

| suite | assertions | cases |
|---|---|---|
| test_image_enhancement | 6220 | 13 |
| test_speckle_filter | 15869 | 26 |
| test_registration_operators | 37 | 4 |
| test_grid_operators | 75 | 5 |
| test_resampler | 283 | 9 |
| test_sar_coregistration | 20555 | 4 |
| test_sar_platform10 | 2766 | 11 |
| test_sar_platform11 | 351 | 5 |
| test_operator_nodata_semantics (r4) | 254 | 7 |
| test_known_answer_corpus_r4 | 269 | 8 |
| test_algorithm_meta_drift | 7761 | 1 |
| test_obia_operators | 159 | 17 |
| test_satellite_products | 498 | 21 |
| test_data_manager_reap | 134 | 14 |

## 5. Sanitizer evidence (crash path)

- valgrind: unavailable on this host — no system valgrind; a user-local
  valgrind 3.25.1 (Arch package) aborts at startup because CachyOS's
  optimized ld.so is stripped and no matching debug package exists (the
  Arch `glibc-debug` build-id does not match).
- Scoped **ASan+UBSan** build (`build-sanitizer`, repo `sanitizer-debug`
  preset: `ENABLE_SANITIZERS=ON`, `-fsanitize=address,undefined
  -fno-sanitize=vptr -fno-omit-frame-pointer`; global
  `CMAKE_POSITION_INDEPENDENT_CODE=ON` needed because lsicnu_grader.a is
  absorbed into a shared lib), target `test_fused_chain`:
  - `[fused_chain][equivalence]` → **All tests passed (40 assertions), zero
    sanitizer reports** (ASAN_OPTIONS leak=0, odr=0 — Qt moc `staticMetaObject`
    ODR noise across the prebuilt system Qt; UBSan silent).
  - full binary → **All tests passed (66 assertions in 4 test cases),
    zero sanitizer reports** — covers the fused streaming executor, chunk
    pipeline threads, GDAL window reads/writes and both operator runs.
- Honest limitation, recorded: the #1356 harness UAF itself is NOT visible to
  ASan — the dangling dereference happens inside the UNINSTRUMENTED system
  Qt6Core (`QCoreApplication::applicationFilePath()`), and with
  `detect_stack_use_after_return=1` the dead frame stays mapped. A reverted
  (pre-fix) sanitizer binary reproduces NO report and passes — so the
  harness-UB evidence remains the pre-fix deterministic SIGSEGV + gdb
  backtrace (§1), which pins the defect unambiguously, plus the post-fix
  green runs. The sanitizer artifact's value is for the PRODUCT paths of this
  track (fused block/window/buffer lifetimes): clean.

## 6. Performance

No hot-path cost introduced where it matters: the sentinel pre-wash runs only
when a band declares a finite sentinel (one extra tile-sized pass; the wash
replaces the per-pixel sentinel comparisons those kernels would otherwise
never do) and the warp contract adds two argv strings to one GDALWarp call.
The fused chain keeps queueCapacity=2 streaming with one added per-tile
observer pass (an O(tile) loop, same order as the kernel itself). Debug-build
timings of the fused equivalence case are unchanged (sub-second for a
333×217 raster, before and after).
