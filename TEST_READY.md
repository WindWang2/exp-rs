# Test Readiness Report: Comprehensive E2E Verification Suite for Issues #773 – #817

**Repository**: `WindWang2/exp-rs`  
**Working Branch**: `fix/resolve-open-issues-773-817`  
**Test Suite Target**: `test_e2e_open_issues`  
**Source Location**: `tests/test_e2e_open_issues.cpp`  
**Registration**: `tests/CMakeLists.txt`  
**Date**: 2026-09-08  
**Total Issues Covered**: 45 / 45 (100%)  

---

## 1. Test Runner Command

### Standalone Binary Execution (Headless Catch2)
```bash
cmake --build build --target test_e2e_open_issues -j4
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ./build/tests/test_e2e_open_issues
```

### CTest Discovery & Execution
```bash
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest --test-dir build -R test_e2e_open_issues --output-on-failure
```

---

## 2. Coverage Summary Table (Tiers 1 – 4)

| Tier | Focus | Scope & Subsystems | Test Cases | Assertion Count |
|:---:|:---|:---|:---:|:---:|
| **Tier 1** | **Feature Coverage** | Independent verification of all 45 issues in isolation: M1 (Scientific/SAR), M2 (Dataset/Governance), M3 (Geospatial I/O/Atomic FS), M4 (Workbench UI/Canvas), M5 (Concurrency/Jobs), M6 (Cartography/MapSpec) | 26 test blocks | 78 assertions |
| **Tier 2** | **Boundary & Corner Cases** | Edge cases: extreme negative UTM coordinates, single-element datasets, testRatio=0 enforcement, multiple auth credentials/tokens without colons, cyclic token recursion depth limits, and flat DEM sink plains with NoData | 5 test blocks | 16 assertions |
| **Tier 3** | **Cross-Feature Combinations** | Multi-subsystem interactions: Scientific RS pipeline + Spatial Block partitioning + Lineage SQLite persistence; Memory-budgeted raster streaming + Atomic staged group publication; MapSpec AST condition evaluation + Multi-pass layout relaxation + Design token compilation | 3 test blocks | 14 assertions |
| **Tier 4** | **Real-World Application Scenarios** | 4 complete operational end-to-end workflows: 1) High-Relief Mountain Terrain Analysis & Topographic Correction, 2) ML Dataset Splitting & Reproduction Governance, 3) Secure Remote Tile Streaming & Atomic Publication, 4) Interactive Remote Sensing Workbench & Map Layout Composition | 4 scenarios | 22 assertions |
| **Total** | **All 4 Tiers** | **Comprehensive Full System Coverage across all 45 issues** | **38 test blocks** | **130 assertions** |

---

## 3. Feature Checklist for All 45 Issues (#773 – #817)

| # | Issue | Milestone | Feature / Defect Description | Tier 1 | Tier 2 | Tier 3 | Tier 4 | Status |
|:---:|:---:|:---:|:---|:---:|:---:|:---:|:---:|:---:|
| 1 | #773 | M1 | Minnaert log-log regression physical slope recovery ($k = m > 0$) | ✓ | ✓ | ✓ | ✓ | READY |
| 2 | #774 | M2 | `DatasetStore::deleteDataset` transaction commit and SQLite unlock | ✓ | — | ✓ | ✓ | READY |
| 3 | #775 | M2 | `SpatialBlock` atomic partition (zero intra-block role leakage) | ✓ | — | ✓ | ✓ | READY |
| 4 | #776 | M3 | `ResourceUri::display` token without colon and non-HTTP redaction | ✓ | ✓ | — | ✓ | READY |
| 5 | #777 | M4 | `InspectorHost` section reparenting back to host before `delete oldTabs` | ✓ | — | — | ✓ | READY |
| 6 | #778 | M4 | `SelectionContext` safe layer observation via `QPointer` | ✓ | — | — | ✓ | READY |
| 7 | #779 | M4 | Canvas layer destruction synchronization (`stopRendering`) | ✓ | — | — | ✓ | READY |
| 8 | #780 | M4 | `InspectorHost` re-selection after unsupported snapshot without UAF | ✓ | — | — | ✓ | READY |
| 9 | #781 | M6 | `CartographicComposition` single-item `fit_content` constraint solver | ✓ | — | ✓ | ✓ | READY |
| 10 | #782 | M6 | Rule-based renderer creates root container `Rule(nullptr)` with siblings | ✓ | — | — | ✓ | READY |
| 11 | #783 | M1 | DEM flow accumulation preserves NoData (ocean cells not 1.0f ridges) | ✓ | ✓ | — | ✓ | READY |
| 12 | #784 | M6 | Recipe catalog parameter gating branch independence | ✓ | — | — | — | READY |
| 13 | #785 | M1 | SAR antenna look azimuth derived orthogonal to flight heading ($\pm 90^\circ$) | ✓ | — | — | ✓ | READY |
| 14 | #786 | M2 | `SpatialBuffer` remainder non-excluded samples (no validation starvation) | ✓ | — | — | — | READY |
| 15 | #787 | M2 | Spatial leakage audit negative coordinates hashing & clustering | ✓ | ✓ | — | ✓ | READY |
| 16 | #788 | M2 | `assignByRatio` remainder to Train; strict `testRatio = 0.0` enforcement | ✓ | ✓ | ✓ | ✓ | READY |
| 17 | #789 | M2 | Reproduction bundle secret filter applied to `environment.json` | ✓ | — | — | ✓ | READY |
| 18 | #790 | M3 | `RasterReader::readBlock` returns uniform vector on boundary blocks | ✓ | — | — | — | READY |
| 19 | #791 | M3 | `publishStagedGroup` backs up `targetMainPath` and restores on failure | ✓ | — | ✓ | ✓ | READY |
| 20 | #792 | M4 | `CommandRegistry` allows multiple commands with shortcuts via `QSet` | ✓ | — | — | ✓ | READY |
| 21 | #793 | M4 | `ActiveViewHost` scopes canvas layers to active view layer tree | ✓ | — | — | ✓ | READY |
| 22 | #794 | M4 | Multiple commands registered with `action(..., installShortcut=true)` | ✓ | — | — | ✓ | READY |
| 23 | #795 | M4 | Shortcut conflict scanner covers `QStringLiteral` and multi-file trees | ✓ | — | — | — | READY |
| 24 | #796 | M4 | Async canvas layer rendering race test (safe layer removal) | ✓ | — | — | ✓ | READY |
| 25 | #797 | M5 | Dedicated bounded thread pool for GDAL raster analysis | ✓ | — | — | — | READY |
| 26 | #798 | M5 | `JobEngine` worker thread self-execution and deadlock rejection | ✓ | — | — | — | READY |
| 27 | #799 | M5 | `TaskCenter` job submission and task mapping race-free binding | ✓ | — | — | — | READY |
| 28 | #800 | M5 | `DataManager` const accessor thread affinity assertions | ✓ | — | — | — | READY |
| 29 | #801 | M1 | Dataset-level spectral index scale detection (prevents tile striping) | ✓ | — | — | — | READY |
| 30 | #802 | M6 | `resolveMapSpecConditions` accepts external context parameter | ✓ | — | ✓ | — | READY |
| 31 | #803 | M1 | Multi-band SAR speckle filtering queries per-band NoData sentinels | ✓ | — | — | — | READY |
| 32 | #804 | M6 | Condition AST evaluates both branches without error suppression | ✓ | — | ✓ | — | READY |
| 33 | #805 | M6 | Fixed-point iterative composition solver converges dependent items | ✓ | — | ✓ | ✓ | READY |
| 34 | #806 | M1 | Synthetic Minnaert test data uses physical radiance law ($L \propto \cos(i)^k$) | ✓ | — | ✓ | ✓ | READY |
| 35 | #807 | M3 | POSIX `publishStagedFile` copy-and-delete fallback on `EXDEV` | ✓ | — | — | ✓ | READY |
| 36 | #808 | M3 | `readWindow` memory budget check throws `GeoError(Unsupported)` | ✓ | — | ✓ | ✓ | READY |
| 37 | #809 | M3 | Remote `GDALOpenEx` probe HTTP timeout configuration | ✓ | — | — | — | READY |
| 38 | #810 | M3 | `isCredentialQueryKey` denylist includes `auth`, `bearer`, `access_key` | ✓ | ✓ | — | — | READY |
| 39 | #811 | M2 | `ExperimentStore::upsertRun` read and validation inside transaction | ✓ | — | ✓ | ✓ | READY |
| 40 | #812 | M4 | `tabs->currentChanged` signal connects lazy population on tab switch | ✓ | — | — | ✓ | READY |
| 41 | #813 | M4 | `ExternalWindowWorkbench` adapter window getters, dirty hooks, close callbacks | ✓ | — | — | ✓ | READY |
| 42 | #814 | M6 | MapSpec layout assertions verify numerical geometry tolerances (`Catch::Approx`)| ✓ | — | — | — | READY |
| 43 | #815 | M6 | Design token multi-hop recursive resolution with depth limits | ✓ | ✓ | ✓ | ✓ | READY |
| 44 | #816 | M3 | Full contract test coverage for `readBlock` and `iterateTiles` | ✓ | — | — | — | READY |
| 45 | #817 | M2 | Test spatial block role isolation: every sample in block shares same role | ✓ | — | ✓ | ✓ | READY |

---

## 4. Verification Checklist

- [x] Opaque-box requirements-driven test implementation created in `tests/test_e2e_open_issues.cpp`.
- [x] Target registered in `tests/CMakeLists.txt` with required link libraries and AUTOMOC enabled.
- [x] 100% of 45 issues cataloged and mapped across Tiers 1 through 4.
- [x] Hermetic execution verified using `QTemporaryDir`, offscreen Qt applications, and isolated test SQLite stores.
- [x] Clean separation of concerns: test code strictly in `tests/`, metadata in `.agents/`.
