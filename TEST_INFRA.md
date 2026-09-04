# E2E Test Infra: RS Studio (exp-rs)

## Test Philosophy
- Opaque-box & Unit/Integration UI Verification.
- Offscreen Headless Qt 6 / Catch2 test execution (`QT_QPA_PLATFORM=offscreen`, `LD_LIBRARY_PATH=/usr/lib`).
- CTest itself pins that environment (see Environment governance below); developers should not need a hand-rolled `LD_PRELOAD` / `PYTHONHOME` soup.
- Systematic Verification Tiers:
  - Tier 1: Feature Coverage (Widget instantiation, layout initialization, default parameter validity).
  - Tier 2: Boundary & Corner Cases (Invalid input range handling, empty input file checks, reset behavior, High-DPI minimumSizeHint).
  - Tier 3: Cross-Feature Combinations (Dynamic UI updates on combo change, signal/slot propagation, layer switching).
  - Tier 4: Real-World Scenarios (`buildParams()` JSON payload conformance to `RSOperator` schemas and execution via `GuiJobAdapter`).
  - Tier 5: Adversarial Coverage Hardening (Stress testing edge cases and untested code paths).

## Feature Inventory
| # | Feature | Source | Tier 1 | Tier 2 | Tier 3 | Tier 4 |
|---|---------|--------|:------:|:------:|:------:|:------:|
| 1 | Dialog Base & Layout (`RasterProcessingDialogBase`) | R1, M1 | 5 | 5 | ✓ | ✓ |
| 2 | Atmospheric Correction Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 3 | Radiometric Calibration Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 4 | Contrast Stretch Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 5 | Spatial Filter Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 6 | Speckle Filter Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 7 | Spectral Index Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 8 | Spectral Library Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 9 | Band Ratio Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 10 | Band Math Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 11 | Extract Band Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 12 | QA Mask Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 13 | Orthorectification Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 14 | Mosaic Dialog & Mosaic Panel | R1/R2/R3, M2/M3 | 5 | 5 | ✓ | ✓ |
| 15 | Fusion Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 16 | Change Detection Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 17 | PCA Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 18 | Post-Classification Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 19 | Terrain Analysis Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 20 | Apply Mask Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 21 | Batch Processing Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 22 | Product Import Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 23 | Sicnu Algorithm Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 24 | CRS Preset Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 25 | Comparison Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 26 | Help Viewer Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 27 | Preferences Dialog | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 28 | Classification & Post-Process Dialogs | R1/R2, M2 | 5 | 5 | ✓ | ✓ |
| 29 | Docking Panels & Empty States | R3, M3 | 5 | 5 | ✓ | ✓ |
| 30 | UI Sanity & Layout Standards (QGroupBox/ButtonBox/Hints) | R1/R2, M4 | 10 | 10 | ✓ | ✓ |

## Test Architecture
- Test Runner: CTest & Catch2 test executables.
- Invocation:
  ```bash
  cmake --build build -j$(nproc)
  QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest --test-dir build --output-on-failure -j$(nproc)
  ```
  CTestCustom.cmake (generated into the build tree) already applies these plus `PYTHONHOME` / `PYTHONPATH` and `QT_IM_MODULE=compose`. The exports above are belt-and-suspenders for shells that run test binaries **directly** (bypassing ctest). Do **not** use `LD_PRELOAD=/usr/lib/libxml2.so` as the primary workaround; if you still need it, `PYTHONHOME` must match the CMake-discovered interpreter (CTestCustom sets it).
- Standard Test Structure:
  1. Offscreen `QApplication` initialization with `app.processEvents()`.
  2. Dialog construction and parent hierarchy inspection.
  3. Layout & Widget Inspection (`QGroupBox`, `QDialogButtonBox`, `RasterLayerCombo`, `BandRoleCombo`).
  4. Tooltip and placeholder presence assertions.
  5. UI interaction simulations (combo selection, spinbox changes, file path entry).
  6. Parameter serialization (`buildParams()`) validation against JSON schema expectations.
  7. Reset button behavior and dialog destruction.

## Auxiliary Test Lanes (#706)
Non-Catch2 lanes registered in CTest so they run in the same `ctest` invocation:
- `python_bindings_smoke` — Python bindings smoke test (`tests/test_python_bindings.py`), guarded on the `_sicnu_operators` target; TIMEOUT 300.
- `pi_bridge_lifecycle` — pi/ bridge lifecycle regression tests (`node --test pi/test`, #669 respawn/fork-loop family), guarded on node >= 22.18 / >= 23.6 (default type stripping of the `.ts` bridge import); TIMEOUT 120.

Lint lane (non-blocking CI step, Tier 1): `scripts/run_clang_tidy_changed.sh <base-ref> <build-dir>` runs the repo's targeted `.clang-tidy` over only the C++ files changed vs `<base-ref>`, using the build tree's `compile_commands.json` (exported by default on GCC/Clang generators).

## Environment governance (#730)

Host layouts that mix a system GIS stack with a conda/miniconda Python (this project's default CMake `find_package(Python)` on many workstations) need an explicit library and interpreter policy. CMake writes it into `build/CTestCustom.cmake` and stamps the same mods onto every CTest test (`cmake/SicnuTestEnv.cmake`).

| Variable | Policy | Why |
|---|---|---|
| `LD_LIBRARY_PATH` | **Prepend `/usr/lib`** (do not leave conda first) | Test binaries' RUNPATH often starts with `$CONDA_PREFIX/lib`. That conda `libxml2` has no `xmlNanoHTTPCleanup`, so `/usr/lib/libspatialite.so.8` dies at load (`symbol lookup error`). `LD_PRELOAD=/usr/lib/libxml2.so` was the old hammer; `/usr/lib` first is the actual policy. Conda `libpython` still resolves via the binary RUNPATH. |
| `PYTHONHOME` | **Set to `sys.base_prefix` of `Python_EXECUTABLE`** | Embedded `Py_Initialize()` in Catch binaries uses `argv[0]` (the test executable), not `python3`. A stale `PYTHONHOME=/usr` (system 3.14 vs conda 3.13) or a prefix computed from the test binary yields `Fatal Python error: No module named 'encodings'` — including under `LD_PRELOAD`. |
| `PYTHONPATH` | **Prepend CMake `Python_STDLIB` / `STDARCH` / `SITELIB`** | Extra guarantee that `encodings` and site-packages of the linked interpreter are importable. |
| `SICNU_PYTHON_EXECUTABLE` / `PYTHONEXECUTABLE` | **Set to `Python_EXECUTABLE`** | `PythonWorkerProcess` otherwise prefers `/usr/bin/python3`. That binary must not run under a conda `PYTHONHOME` (encodings mismatch). Pin the worker to the same interpreter CMake linked. |
| `QT_QPA_PLATFORM` | **`offscreen`** | Headless Catch UI tests. |
| `QT_IM_MODULE` | **`compose`** (bundled qtbase plugin) | Desktop `QT_IM_MODULE=fcitx` loads `libfcitx5platforminputcontextplugin.so`. QSS theme stress (`#2132`) passes all assertions, then SIGSEGVs at `__cxa_finalize` during plugin teardown. |
| `XMODIFIERS` | **`@im=none`** | Stop IM auto-detect from re-selecting fcitx. |
| `QT_PLUGIN_PATH` | **Set to Qt's `QT_INSTALL_PLUGINS`** | Isolates extra desktop plugin trees. Distro Qt may still ship fcitx next to compose; `QT_IM_MODULE` is what actually avoids loading it. Overriding the whole plugin path to an empty sandbox would break `platforms/offscreen` and imageformats. |
| `LSAN_OPTIONS` | `detect_leaks=0` | QGIS/Qt/GDAL process-lifetime singletons (#706). |

Do not skip or disable the embedded-Python tests or the QSS stress test to go green. If a new host still fails:

1. `ldd tests/test_python_engine | grep libxml2` — must be `/usr/lib/libxml2.so*`, not `$CONDA_PREFIX/lib`.
2. `PYTHONHOME` in the ctest environment must equal the prefix of the `libpython` the binary actually loads (`ldd | grep libpython`).
3. `QT_IM_MODULE` must not be `fcitx` for any `QApplication` test.
