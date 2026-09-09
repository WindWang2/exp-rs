# Project: Comprehensive Resolution of All 45 Open Issues (#773 - #817)

## Architecture
- **Scientific RS Algorithms**: Pure computational and processing kernels for radiometric/topographic correction, digital elevation modeling, SAR antenna and speckle geometry, and spectral indices. Decoupled from GUI and database dependencies.
- **Dataset & Experiment Store**: SQLite persistence stores for datasets and experiment run lineage, spatial partitioning algorithms (grid block, spatial buffer, stratified), and reproduction bundle serialization with credential denylists.
- **Geospatial I/O & Atomic Storage**: URL/URI parsing, credential maskers, memory-budgeted raster window and block readers, and POSIX/Windows cross-filesystem atomic file publishing.
- **Workbench UI & Rendering Synchronization**: Qt 6 GUI workspace, including `InspectorHost` tab lifecycle management, `SelectionContext` safe layer observation via `QPointer`, `ActiveViewHost` / `DisplayManager` asynchronous canvas rendering synchronization, and `CommandRegistry` shortcut uniqueness.
- **Concurrency & Job Engine**: Thread pool resource management, worker deadlock prevention on synchronous job waits, race-free `TaskCenter` job dispatching and callback binding, and `DataManager` thread affinity assertions.
- **Cartography & MapSpec Compiler**: Fixed-point iterative layout constraint solver, QGIS rule-based renderer tree hierarchy, recipe parameter gating, short-circuit safe AST evaluation, and recursive design token resolution.

## Feature Inventory
| # | Issue | Feature / Defect | Description | Milestone | Source |
|---|---|---|---|---|---|
| 1 | #773 | Minnaert slope inversion | Minnaert log-log regression uses `kk = -m`, inverting physical slope | M1 | Survey 1 |
| 2 | #774 | Dataset deletion commit | `deleteDataset` begins SQLite transaction but never commits on success | M2 | Survey 1 |
| 3 | #775 | SpatialBlock atomic partition | `SpatialBlock` splits samples internally within blocks instead of atomic block partition | M2 | Survey 1 |
| 4 | #776 | ResourceUri credential display | Cleartext token if no colon; non-HTTP URIs skip display redaction | M3 | Survey 1 |
| 5 | #777 | InspectorHost section UAF | `delete oldTabs` destroys registered `InspectorSection` children | M4 | Survey 1 |
| 6 | #778 | SelectionContext layer pointers | Raw pointers and 150ms cache return deleted `QgsMapLayer` instances | M4 | Survey 1 |
| 7 | #779 | Canvas layer destruction race | Layer destroyed while canvas background render thread is active | M4 | Survey 1 |
| 8 | #780 | InspectorHost re-selection test | Test stops after unsupported snapshot, masking UAF | M4 | Survey 1 |
| 9 | #781 | Cartography fit_content solver | Constraint solver drops single-item `fit_content` constraint | M6 | Survey 1 |
| 10 | #782 | Rule-based renderer hierarchy | User rules appended to rule 0 instead of root container | M6 | Survey 1 |
| 11 | #783 | Flow accumulation NoData | Ocean/NoData cells initialized to 1.0f creating fake ridges | M1 | Survey 1 |
| 12 | #784 | Recipe anyGateClosed flag | Global `anyGateClosed` forces skipped parameters across parallel branches | M6 | Survey 1 |
| 13 | #785 | SAR flight heading vs look azimuth | Platform heading used as antenna look azimuth (90° error) | M1 | Survey 1 |
| 14 | #786 | SpatialBuffer excluded remainder | `remaining` includes `excluded` buffer vetoes, starving validation | M2 | Survey 1 |
| 15 | #787 | Leakage audit negative hashing | Integer division truncation toward zero corrupts negative coordinates | M2 | Survey 1 |
| 16 | #788 | assignByRatio remainder to Test | `floor()` dumps remainders and singletons into Test, violating testRatio=0 | M2 | Survey 2 |
| 17 | #789 | Reproduction bundle secret filter | Export boundary writes `environment.json` without `filterSecrets()` | M2 | Survey 2 |
| 18 | #790 | RasterReader readBlock edge truncation | `readBlock` returns truncated vector on edge blocks smaller than blockSize | M3 | Survey 2 |
| 19 | #791 | publishStagedGroup main backup | `publishStagedGroup` backs up sidecars but never backs up `targetMainPath` | M3 | Survey 2 |
| 20 | #792 | CommandRegistry shortcut assertion | Single `QString m_shortcutOwner` aborts process on second shortcut | M4 | Survey 2 |
| 21 | #793 | refreshCanvasLayers project checked | Reads global `QgsProject` checked layers, breaking multi-view isolation | M4 | Survey 2 |
| 22 | #794 | Test command registry shortcuts | Test suite never tested `action(..., installShortcut=true)` on multiple commands | M4 | Survey 2 |
| 23 | #795 | Shortcut conflict scanner coverage | Regex misses `QStringLiteral` and scans only `main_window_menus.cpp` | M4 | Survey 2 |
| 24 | #796 | Async layer rendering race test | Test suite checks only synchronous pointer updates, omitting async render race | M4 | Survey 2 |
| 25 | #797 | Unbounded global thread pool GDAL | Long GDAL operations on global thread pool starve canvas rendering | M5 | Survey 2 |
| 26 | #798 | Worker pool deadlock on sub-jobs | Worker calling synchronous wait causes thread pool starvation deadlock | M5 | Survey 2 |
| 27 | #799 | Race in submit() and task mapping | Rapid job completion drops notification before `m_taskByJobId` mapped | M5 | Survey 2 |
| 28 | #800 | DataManager const thread affinity | Const accessors lack thread affinity assertions, risking torn reads | M5 | Survey 2 |
| 29 | #801 | Block-level spectral index scaling | `isScaledReflectance` per tile causes striping seams across tiles | M1 | Survey 2 |
| 30 | #802 | Condition context redundant requirement | Early exit if `!spec.isMember("condition_context")` drops external context | M6 | Survey 2 |
| 31 | #803 | Multi-band speckle NoData reuse | Multi-band loop reuses `nodata` sentinel computed once from band 1 | M1 | Survey 3 |
| 32 | #804 | Condition AST short-circuit errors | C++ `&&`/`\|\|` short-circuit skips right child, swallowing syntax errors | M6 | Survey 3 |
| 33 | #805 | Constraint solver linear order | Single-pass constraint resolution fails dependent item convergence | M6 | Survey 3 |
| 34 | #806 | Synthetic Minnaert test data | Test fixture inverts physical radiance to mask inverted slope (coupled #773) | M1 | Survey 3 |
| 35 | #807 | POSIX atomic rename cross-device | POSIX `publishStagedFile` fails with `EXDEV` across filesystems | M3 | Survey 3 |
| 36 | #808 | readWindow memory budget check | `readWindow` lacks memory budget check, risking uncaught `std::bad_alloc` | M3 | Survey 3 |
| 37 | #809 | Remote GDALOpenEx HTTP timeout | Remote probe lacks HTTP timeout config, risking indefinite thread hang | M3 | Survey 3 |
| 38 | #810 | isCredentialQueryKey common keys | Query key denylist omits `"auth"`, `"bearer"`, `"access_key"` | M3 | Survey 3 |
| 39 | #811 | loadRunLocked outside transaction | Validation runs outside SQLite transaction, creating TOCTOU race | M2 | Survey 3 |
| 40 | #812 | tabs->currentChanged not connected | `tabs->currentChanged` never connected; secondary inspector tabs blank | M4 | Survey 3 |
| 41 | #813 | ExternalWindowWorkbench adapter hooks | Workbench instances lack window getters, dirty hooks, and close callbacks | M4 | Survey 3 |
| 42 | #814 | Test assertions numerical tolerances | Tests check JSON non-null rather than numerical geometry tolerances | M6 | Survey 3 |
| 43 | #815 | Design token multi-hop recursion | `resolveTokensRecursive` stops at depth 1, missing alias tokens | M6 | Survey 3 |
| 44 | #816 | Tests for readBlock and iterateTiles | 0% test coverage for `readBlock` and `iterateTiles` in RasterReader | M3 | Survey 3 |
| 45 | #817 | Test spatial block role isolation | Tests check sample count and seed, omitting block-to-role consistency check | M2 | Survey 3 |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Scientific & Remote Sensing Algorithms | #773, #783, #785, #801, #803, #806 | none | DONE |
| M2 | Dataset, Experiment Store & Spatial Split Lineage | #774, #775, #786, #787, #788, #789, #811, #817 | none | DONE |
| M3 | Geospatial I/O, Raster Contracts & Atomic Filesystem | #776, #790, #791, #807, #808, #809, #810, #816 | none | DONE |
| M4 | Workbench UI, Display Manager & Canvas Synchronization | #777, #778, #779, #780, #792, #793, #794, #795, #796, #812, #813 | none | DONE |
| M5 | Concurrency, Threading & Job Engine | #797, #798, #799, #800 | none | DONE |
| M6 | Cartography, Layout Composition & MapSpec Compiler | #781, #782, #784, #802, #804, #805, #814, #815 | none | DONE |
| M7 | Final E2E Integration & Issue Closure | Pass 100% E2E tests, clean merge to master, close #773-#817 | M1-M6, E2E | PLANNED |

## Interface Contracts
### Scientific Algorithms ↔ Pipeline & Operators
- `MinnaertRegression`: $L \propto (\cos i)^k \implies m = k > 0$. Fit returns true with $k = m$ for physical terrain.
- `flowAccumulation`: Cells with NoData sentinel are propagated with NoData value, omitting them from drainage calculation.
- `terrainGeometry`: SAR look azimuth $\phi_{\text{look}} = \phi_{\text{heading}} \pm 90^\circ$ derived from heading and look direction.
- `savi` / `evi`: Dataset-level scale detection sets normalization parameters uniformly across all blocks.
- `speckleRaster`: Per-band NoData sentinel retrieval preserves distinct band sentinels.

### Dataset & Experiment Stores ↔ Clients
- `DatasetStore::deleteDataset`: Enclosed in explicit `begin` and `commit` transaction; clean rollback on any failure.
- `SpatialBlock`: Groups samples by grid cell and allocates whole blocks atomically to split roles.
- `assignByRatio`: Largest Remainder Method distributes samples deterministically; zero testRatio yields strictly 0 test samples.
- `reproduction_bundle`: Run environment variables are filtered through `filterSecrets()` before export.
- `ExperimentStore::upsertRun`: Read, validation, and write execute inside SQLite transaction.

### Geospatial I/O ↔ Callers
- `ResourceUri::display`: Redacts sensitive credentials (passwords, tokens, auth/bearer query params) across HTTP, S3, Postgres, and FTP URIs.
- `RasterReader::readBlock`: Returns a vector of exactly `blockSize.first * blockSize.second` elements, padding boundary blocks with NoData.
- `RasterReader::readWindow`: Accepts optional `maxBytes` budget, throwing `GeoError(ErrorCode::Unsupported)` when exceeded.
- `publishStagedGroup`: Backs up `targetMainPath` alongside sidecars and safely rolls back on failure.
- `publishStagedFile`: Handles cross-device filesystem moves (`EXDEV`) via copy-and-delete fallback on POSIX.

### Workbench UI & Display ↔ QGIS Map Canvas
- `InspectorHost`: Reparents registered `InspectorSection` instances back to `this` before destroying `oldTabs`, avoiding UAF.
- `InspectorHost::rebuildTabs`: Connects `tabs->currentChanged` to lazily populate secondary sections upon selection.
- `SelectionContext`: Replaces raw widget pointers with `QPointer`; listens to `layersWillBeRemoved` to immediately evict deleted layers.
- `QgisDisplayManager`: Calls `canvas->stopRendering()` prior to deleting/removing map layers.
- `CommandRegistry`: Tracks `QSet<QString> m_shortcutOwners`, allowing each command ID to project its shortcut once without collision.
- `ActiveViewHost`: Scopes canvas layers to the active view's layer tree rather than global project checked layers.

### Concurrency ↔ Processing Framework
- `RoiStatisticsWidget` / `HistogramWidget`: Runs GDAL operations on a dedicated, bounded thread pool with cooperative cancellation.
- `JobEngine`: Worker threads detect self-execution and reject synchronous child waits, preventing worker pool deadlocks.
- `TaskCenter`: Binds `taskId` before or with job submission so `onJobRecord` never drops early completion callbacks.
- `DataManager`: Asserts `QThread::currentThread() == thread()` on all const query accessors.

### Cartography ↔ MapSpec Compiler
- `CartographicComposition`: Solver executes multi-pass relaxation (up to 10 passes) to converge ordered/dependent constraints; supports single-item `fit_content`.
- `StyleCompiler`: Rule-based renderer creates an empty container root `Rule(nullptr)` and appends user rules as sibling children.
- `RecipeCatalog`: Step parameter fallback depends strictly on the step's own gate status.
- `MapSpecCondition`: Evaluates both branches of `&&` / `\|\|` without short-circuit suppression of syntax/property errors.
- `StyleSpec`: Multi-hop recursive token resolution resolves aliased design tokens with depth capping.

## Code Layout
- `src/processing/algorithms/`: Scientific RS algorithms (M1)
- `src/operators/rs/`: Remote sensing pipeline operators (M1)
- `src/dataset/`: Dataset store, splitting, and leakage audit (M2)
- `src/experiment/`: Experiment governance and reproduction bundle (M2)
- `src/geospatial/raster/`: Raster reader and memory bounds (M3)
- `src/geospatial/util/`: Resource URI and atomic filesystem (M3)
- `src/geospatial/probe/`: Resource probe and timeouts (M3)
- `src/app/workbench/`: Workbench host, inspector, command registry, selection (M4)
- `src/app/display/`: Display manager and canvas layer synchronization (M4)
- `src/app/active_view_host.cpp`: Active view and canvas multi-view layer routing (M4)
- `src/app/widgets/`: ROI statistics and histogram background widgets (M5)
- `src/jobs/`: JobEngine worker pool and concurrency (M5)
- `src/processing/framework/task_center.cpp`: Task dispatching and mapping (M5)
- `src/data/data_manager.cpp`: DataManager thread affinity assertions (M5)
- `src/agent/cartography/`: Composition constraint solver and style compiler (M6)
- `src/agent/mapspec/`: MapSpec conditions, AST evaluation, and token spec (M6)
- `src/agent/harness/recipe_catalog.cpp`: Recipe step gate evaluation (M6)
- `tests/`: Catch2 unit and E2E test suites
