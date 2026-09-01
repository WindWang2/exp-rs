# Project: RS Studio (exp-rs) UI Visual & Human-Centric UX Modernization

## Architecture
- **Framework**: Qt 6.8+ (Core, Gui, Widgets), C++20, CMake 3.24+, Catch2 v3.7.1.
- **UI Structure**:
  - Main Window: Ribbon dock (`rsRibbonDock`), Toolbar strip (`RsToolbarFlowHost`), Main Map View Splitter (`rsMapViewSplitter`), Docking Panels.
  - Docking Panels: Layer Tree (`m_layersDock`), Data Manager (`DataManagerDock`), Task Center (`TaskCenterDock`), Log Panel (`LogDock`), Guided Workflows, Pipeline Editor, AI Copilot.
  - Dialog Base & Helpers: `RasterProcessingDialogBase`, `SicnuUi`, `RasterLayerCombo`, `BandRoleCombo`, `FileBrowseWidget`, `DialogHelpCatalog`.
  - RS Processing Dialogs (37 Dialog Classes): Atmospheric, Calibration, Stretch, Filters, Spectral, Band Math/Ratio, Ortho, Mosaic, Fusion, Change Detection, PCA, Post-Classification, Terrain, Mask, Batch, Import, Algorithm, CRS, Preferences, Comparison, Help, etc.
- **Backend & Data Flow**:
  - Preserves `ProjectContext`, `RSOperator` JSON parameter contracts, and `JobEngine` async pipeline unchanged.

## Code Layout
- `src/app/dialogs/`: Base dialogs, utilities, and all RS processing dialog implementations.
- `src/app/widgets/`: Reusable UI widgets (`RasterLayerCombo`, `BandRoleCombo`, `FileBrowseWidget`, `SpectralProfileWidget`, `HistogramWidget`, `GuidedWorkflowWidget`, `RsEmptyStateWidget`).
- `src/app/panels/`: Docking panels (`DataManagerPanel`, `MosaicPanel`, `LogPanel`, `LayerTreeDock`, `TaskCenterDock`).
- `src/app/shell/`: Main Window (`MainWindow.cpp/.h`), ribbon dock, toolbar host, job panel.
- `src/app/workflow/`: `PipelineEditorDock`.
- `src/agent/`: `AgentCopilotDockWidget`.
- `resources/`: `styles.qss`, `styles-dark.qss`, vector icons, resources.qrc.
- `tests/`: Catch2 unit and offscreen UI test suites (`tests/CMakeLists.txt`).

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Dialog Base Layout Standardization | Refactor `SicnuUi` & `RasterProcessingDialogBase` to use standard `QGroupBox`, `QDialogButtonBox`, dynamic `minimumSizeHint`, standardized margins (10~14px), row spacing (6~10px), right-aligned form labels | M1 | R1, survey_1 |
| 2 | Reusable Component Integration | Standardize in-dialog `RasterLayerCombo` & `BandRoleCombo` integration in dialog base and reusable patterns | M1 | R2, survey_1 |
| 3 | Test Infra & Syntax Fix | Fix `test_histogram_stretch_widget.cpp` orphan `else` syntax error and introduce `sicnu_add_dialog_test` CMake test helper | M1 | R4, survey_3 |
| 4 | Processing Dialogs Batch A (Spectral/Radiometric/Filter) | Standardize `AtmosphericDialog`, `RadiometricCalibrationDialog`, `ContrastStretchDialog`, `SpatialFilterDialog`, `SpeckleFilterDialog`, `SpectralIndexDialog`, `SpectralLibraryDialog`, `BandRatioDialog`, `BandMathDialog`, `ExtractBandDialog`, `QaMaskDialog` | M2 | R1/R2, survey_1 |
| 5 | Processing Dialogs Batch B (Geometric/Mosaic/Classification/Tools) | Standardize `OrthorectificationDialog`, `MosaicDialog`, `FusionDialog`, `ChangeDetectionDialog`, `PcaDialog`, `PostClassificationDialog`, `TerrainDialog`, `ApplyMaskDialog`, `BatchProcessingDialog`, `ProductImportDialog`, `SicnuAlgorithmDialog`, `CrsPresetDialog`, `ComparisonDialog`, `HelpViewerDialog`, `PreferencesDialog`, `RsMergeClassesDialog`, `RsPostProcessDialog`, `RsSiftDialog`, `RsTemplateMatchDialog` | M2 | R1/R2, survey_1 |
| 6 | UX & Parameter Validation Polish | Tooltips, placeholders, default presets, value range validation, eliminate hardcoded color stylesheets (`#666`, `#656d76`), clean up English-only messages into Chinese | M2 | R2, survey_1 |
| 7 | Main Window & Splitters Visual Polish | Standardize margins, paddings, QSplitter styling across Canvas, Layer Tree, Data Manager, Task Center, and Log Panel | M3 | R3, survey_2 |
| 8 | Empty States Standardization | Implement `RsEmptyStateWidget` and integrate empty states across Layer Tree, Data Manager, Task Center, Log Panel, and Canvas | M3 | R3, survey_2 |
| 9 | Style & Internationalization Cleanup | Clean up hardcoded dark inline styles (`#1e293b`, `#4CAF50`) in `PipelineEditorDock` & `GuidedWorkflowWidget`, replace Emoji text decorations with vector icons/standard labels, internationalize log & status indicators | M3 | R3, survey_2 |
| 10 | Comprehensive Catch2 Offscreen UI Test Suites | Implement high-fidelity offscreen Catch2 test suites for all 16 untested dialogs, `MosaicPanel`, real `PreferencesDialog`, and UI sanity test suite | M4 | R4, survey_3 |
| 11 | Full Build & Test Verification | 100% green CMake build and Catch2 CTest execution with zero warnings/regressions, adversarial Tier 5 coverage audit | M4 | R4, survey_3 |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Base Infrastructure & Components | Refactor `RasterProcessingDialogBase`, `SicnuUi`, fix test syntax, CMake test helper | none | DONE |
| M2 | Processing Dialogs Standardization | Standardize all 28+ processing dialogs (QGroupBox, QDialogButtonBox, Tooltips, Validation, High-DPI) | M1 | DONE |
| M3 | Main Window, Panels & Empty States | Polish MainWindow, Splitters, Docking Panels, Empty States, remove inline QSS/Emoji | M1 | IN_PROGRESS |
| M4 | E2E Testing Suite & Final Verification | Catch2 tests for all dialogs & panels, layout sanity tests, full 100% green build & verification | M2, M3 | PLANNED |

## Interface Contracts
### Base Classes & UI Helpers
- `SicnuUi::makeGroup(const QString& title, QWidget* parent)` -> Returns `QGroupBox*` with title and unified styling.
- `SicnuUi::makeSection(const QString& title, QWidget* parent)` -> Standardized `QGroupBox*` (replaces raw `QFrame`).
- `RasterProcessingDialogBase`:
  - `setupButtonBar()`: Standard `QDialogButtonBox` with `AcceptRole` ("运行" / "确定"), `RejectRole` ("取消"), `ResetRole` ("重置"), `HelpRole` ("帮助").
  - `setupOutputRow()`: Standard `QGroupBox` titled "输出配置".
  - `minimumSizeHint()`: Returns responsive QSize ensuring no truncation.
  - `buildParams()`: Unchanged contract, returns `QJsonObject` matching backend `RSOperator`.
- `RsEmptyStateWidget`:
  - `RsEmptyStateWidget(const QString& iconName, const QString& title, const QString& description, const QString& actionText = QString(), QWidget* parent = nullptr)`
  - Signal: `actionClicked()`.
