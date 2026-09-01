# E2E Test Infra: RS Studio (exp-rs)

## Test Philosophy
- Opaque-box & Unit/Integration UI Verification.
- Offscreen Headless Qt 6 / Catch2 test execution (`QT_QPA_PLATFORM=offscreen`, `LD_LIBRARY_PATH=/usr/lib`).
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
  LD_LIBRARY_PATH=/usr/lib QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
  ```
- Standard Test Structure:
  1. Offscreen `QApplication` initialization with `app.processEvents()`.
  2. Dialog construction and parent hierarchy inspection.
  3. Layout & Widget Inspection (`QGroupBox`, `QDialogButtonBox`, `RasterLayerCombo`, `BandRoleCombo`).
  4. Tooltip and placeholder presence assertions.
  5. UI interaction simulations (combo selection, spinbox changes, file path entry).
  6. Parameter serialization (`buildParams()`) validation against JSON schema expectations.
  7. Reset button behavior and dialog destruction.
