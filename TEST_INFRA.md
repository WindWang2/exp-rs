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
| 31 | Workspace Browser Wiring (governance dock contract) | UX4-A/D, M1 | ✓ | ✓ | ✓ | ✓ |
| 32 | Band Tool Operators (rs:band_ratio / extract_bands / contrast_stretch) | UX4-C, M1 | ✓ | ✓ | ✓ | ✓ |
| 33 | Schema Form Builder v2 (validation, x-ui-type editors) | UX4-B, M1 | ✓ | ✓ | ✓ | ✓ |
| 34 | RsResultSummary (shared structured result view) | UX4-E, M1 | ✓ | ✓ | ✓ | ✓ |
| 35 | Shortcut Conflicts (shell action host) | UX4-G, M1 | ✓ | ✓ | ✓ | ✓ |

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
| `SICNU_CHECKPOINT_DIR` | **Point at a per-run temp dir in tests / per-session dir in headless sessions** (unset keeps `~/.rs_studio/checkpoints`) | Workflow checkpoints, run-lock files and history live under one directory read per call from this env (workflow_checkpoint.h). Without it, every `run_workflow`/`workflow resume` test writes the developer's real HOME where accumulated lock files from other sessions made the MCP run_workflow lane flaky (#1351 backlog). |

Do not skip or disable the embedded-Python tests or the QSS stress test to go green. If a new host still fails:

1. `ldd tests/test_python_engine | grep libxml2` — must be `/usr/lib/libxml2.so*`, not `$CONDA_PREFIX/lib`.
2. `PYTHONHOME` in the ctest environment must equal the prefix of the `libpython` the binary actually loads (`ldd | grep libpython`).
3. `QT_IM_MODULE` must not be `fcitx` for any `QApplication` test.

## Budget governance (#1361, #1363)

Performance and size budgets are split into two classes with different rules:

| Class | Where measured | Examples | Raise rule |
|---|---|---|---|
| **Product SLO gate** | Release / RelWithDebInfo | `test_workspace_catalog` 100k page contract (500 ms); `test_mlops9_scale` 100k full-scale shape | Raise only with attribution data from the test's own report output |
| **Non-product lane guard** | Debug / sanitizer / envelope | Debug 1000 ms + sanitizer 1500 ms page gates; `test_mlops9_scale` Debug 30k default; `test_catalog_size` per-operator ceiling + 288 KiB envelope | Same rule; keep the product gate reserved for the SLO lane — never widen it to fit local hardware |

The product gates are calibrated on CI-class runners. Slower workstations
have a HOST BASELINE offset that is roughly profile-independent (r5
measurement: the workspace 100k page median is stable at ~0.58-0.61 s on a
loaded workstation in BOTH Debug (578-594 ms) and Release (595-605 ms)) —
expect a stable red on the product gate there, read the printed medians as
attribution, and compare against the CI baseline instead of raising the
gate. Sanitizer builds do roughly double the per-row SQLite cost; their
gates account for that.

### Rules

1. **Never raise a budget to silence a red** — first decide with robust stats
   (median-of-3 passes in-process) whether the red is jitter, instrumentation
   cost, environment, or a real regression. Only the last one is a product
   bug; the other three are budget/process work.
2. **Per-operator ceiling over envelope ratchet** (`test_catalog_size.cpp`):
   the whole-catalog envelope (288 KiB) is a secondary guard. The primary
   guard is the per-operator schema ceiling — a budget change must name the
   operators that outgrew their budget. Run with
   `SICNU_CATALOG_SIZE_REPORT=1` to dump the ranked per-operator size table
   and attach it to the budget-change PR (#1361 acceptance).
3. **Scale profiles, not timeouts** (`test_mlops9_scale.cpp`): the 100k-run
   store keeps its true scale on Release/RelWithDebInfo. Debug (+sanitizer)
   defaults to 30k and asserts the same per-run cost contract, so complexity
   regressions still fail at reduced cost. Full scale in a Debug tree is a
   labeled opt-in, not a timeout raise — ALWAYS pair the override with the
   longrun selector (`-R ..._longrun`); without it the override re-creates
   the #1363 red against the REGULAR registration's 900 s timeout:
   ```bash
   SICNU_MLOPS9_SCALE_RUNS=100000 ctest -R mlops9_scale_longrun -L LONG_RUN
   ```
   Sanitizer note: a sanitizer LONG_RUN session at 100k costs ~2x the Debug
   seed (~2500 s+ measured basis: 12.27 ms/run Debug × 2) and would exceed
   the longrun registration's 2400 s TIMEOUT. No sanitizer LONG_RUN lane
   exists; if one is added, raise that TIMEOUT with attribution data.
4. **Cost-tier labels** (R6, #1392): every discovered ctest case carries one,
   injected as the default `FAST` by `sicnu_discover_tests` unless a
   registration overrides it. Vocabulary (upper-case, per the LONG_RUN
   precedent):
   - `FAST` — the tier-1 signal lane; a bare/fast `ctest` selection.
   - `SLOW` — audited minutes-scale clusters (benchmarks/scale suites,
     TIMEOUT >= 300 s); still required coverage, run by tier 2 and by
     explicit `ctest -L SLOW` sessions.
   - `LONG_RUN` — 20 min+ full-scale sessions (`ctest -L LONG_RUN`).
   Tier-1 CI and the fast local/PR loop select with `ctest -LE "SLOW|LONG_RUN"`;
   tier 2 (sanitizer) runs everything EXCEPT `LONG_RUN` — the 2400 s
   full-scale session is a deliberate CI exclusion (review P1, R6): master's
   unfiltered lanes never completed inside GitHub's 360-min job cap, so the
   session ran only by queue luck; run it explicitly via `ctest -L LONG_RUN`.
   Coverage audit scope: the census gate
   (`.planning/verify-chain-r4/chain_gate_census.sh`, section 5) FAILs any
   DISCOVERY-FUNNEL case without a label; the ~28 bare `add_test` script
   lanes (python/pi/i18n/edit/D16) run unlabeled in BOTH tiers by design
   (label-less tests cannot match `-LE` exclusions) and remain
   hand-countable. `ctest -N -V` shows all labels.
5. **Shared-host timing evidence**: perf contracts measure multiple passes
   and assert on the median (see `test_workspace_catalog.cpp`). When quoting
   timings in a budget discussion, record build type, sanitizer state, and
   host load — a single Debug number on a loaded host is not attribution.
