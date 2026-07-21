# Raster Processing Dialog Base Class

Base class for raster processing dialogs providing common UI elements and callbacks.

## Header

```cpp
#include "dialogs/raster_processing_dialog_base.h"
```

## Usage

Inherit from `RasterProcessingDialogBase` and implement the pure virtual methods:

```cpp
class MyDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit MyDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("my_tool"); }
    QString dialogTitle() const override { return tr("My Tool"); }
    void onRun() override;

private:
    void setupUi();
    // Custom UI members...
};
```

## Constructor Pattern

```cpp
MyDialog::MyDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(tr("My Tool"));
    setupUi();
}

void MyDialog::setupUi()
{
    auto *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(this);
    }

    // Add custom input controls
    auto *inputGroup = new QGroupBox(tr("Input"));
    // ...
    mainLayout->addWidget(inputGroup);

    // Add output file row (from base class)
    setupOutputRow(mainLayout);

    // Add Run/Cancel buttons (from base class)
    setupButtonBar(mainLayout);
}
```

## Base Class Members

| Member | Type | Description |
|--------|------|-------------|
| `m_rasterLayer` | `QgsRasterLayer*` | Current raster layer (set via `setRasterLayer()`) |
| `m_outputEdit` | `QLineEdit*` | Output file path (created by `setupOutputRow()`) |
| `m_runButton` | `QPushButton*` | Run button (created by `setupButtonBar()`) |

## Base Class Methods

### Pure Virtual (must override)

| Method | Description |
|--------|-------------|
| `toolName()` | Return tool name for log messages (e.g. "band_math") |
| `dialogTitle()` | Return dialog title for message boxes |
| `onRun()` | Execute the processing logic |

### Virtual (optional override)

| Method | Description |
|--------|-------------|
| `validateInputs()` | Validate inputs before running. Default checks output path and raster layer. |

### UI Helpers

| Method | Description |
|--------|-------------|
| `setupOutputRow(layout)` | Create output file row (label + line edit + browse button) |
| `setupButtonBar(layout)` | Create Run/Cancel button bar |
| `browseOutput()` | Open file dialog for output path (GeoTIFF filter) |
| `setRasterLayer(layer)` | Set the raster layer to process |
| `outputPath()` | Get the output file path |

### Completion Handlers

| Method | Description |
|--------|-------------|
| `handleCompleted(outputPath)` | Log success, re-enable run button, accept dialog |
| `handleFailed(error)` | Log error, re-enable run button, show error message |
| `onCompleted(outputPath)` | Slot for JobEngine job success |
| `onFailed(errorMessage)` | Slot for JobEngine job failure |

## Async Processing: JobEngine (required path)

All long-running dialog work goes through **JobEngine** so tasks appear in the
**RsJobPanel** dock (cancel, logs, progress). Do **not** start ad-hoc
`std::thread` / `QtConcurrent` / `AsyncGdalRunner` from new dialog code.

### RSOperator path (preferred for modular tools)

```cpp
void MyDialog::onRun()
{
    if (!validateInputs()) return;

    Json::Value params;
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    // ... operator-specific fields

    runOperatorTask(QStringLiteral("rs:my_operator"), params);
}
```

`runOperatorTask` submits `JobRequest` with `algorithmId` = operator id.
On success the job result must include an `"output"` string path; the base class
calls `onCompleted(path)`.

### GDAL lambda path

```cpp
void MyDialog::onRun()
{
    if (!validateInputs()) return;

    QString sourcePath = m_rasterLayer->source();
    QString outPath = outputPath();

    runGdalTask([sourcePath, outPath]() -> QString {
        // Background JobEngine worker (callable:gdal_task)
        // Return non-empty path on success
        // Return "\x01SICNU_ERR\x01" + message for structured failure
        // Return empty for generic failure
        return outPath;
    });
}
```

### Processing algorithm path

- **Toolbox double-click** opens `SicnuAlgorithmDialog`; **Run** prepares the
  algorithm on the GUI thread and submits `runPrepared` to JobEngine with
  algorithm id `processing:<qgisAlgorithmId>`.
- Dialog helpers may call `runAlgorithmTask(algorithm, params, context)` which
  also submits via JobEngine (worker-local context).

Prefix executor registration (app startup):

```cpp
ProcessingJobAdapter::registerProcessingJobExecutor(); // "processing:"
```

### Deprecated legacy runners

| Type | Status |
|------|--------|
| `AsyncGdalRunner` | Deprecated stub; use `runGdalTask` |
| `AsyncAlgorithmRunner` | Deprecated stub; use `SicnuAlgorithmDialog` / `runAlgorithmTask` |

These types remain compiled for transitional tests only.

## Dialog Utility: populateRasterLayerCombo

```cpp
#include "dialogs/dialog_utils.h"

// Populate a combo box with all valid raster layers from the project
populateRasterLayerCombo(m_layerCombo);
```

Each item stores `QgsRasterLayer*` as QVariant data. Use `currentData().value<QgsRasterLayer*>()` to retrieve.

## Examples in Codebase

| Dialog | File | Description |
|--------|------|-------------|
| BandMathDialog | `band_math_dialog.{h,cpp}` | Band math expression evaluator |
| SpectralIndexDialog | `spectral_index_dialog.{h,cpp}` | NDVI, EVI, SAVI, etc. |
| AtmosphericDialog | `atmospheric_dialog.{h,cpp}` | DOS1/DOS2 correction |
| ContrastStretchDialog | `contrast_stretch_dialog.{h,cpp}` | Linear, clip, stddev stretch |
| SpatialFilterDialog | `spatial_filter_dialog.{h,cpp}` | Mean, Gaussian, median filters |
| SpeckleFilterDialog | `speckle_filter_dialog.{h,cpp}` | Lee, Frost, Kuan, Gamma-MAP |
| PcaDialog | `pca_dialog.{h,cpp}` | Principal Component Analysis |
| FusionDialog | `fusion_dialog.{h,cpp}` | Image fusion / pan-sharpening |
| TerrainDialog | `terrain_dialog.{h,cpp}` | Slope, aspect, hillshade |
| ExtractBandDialog | `extract_band_dialog.{h,cpp}` | Extract single band |
| ChangeDetectionDialog | `change_detection_dialog.{h,cpp}` | Multi-temporal comparison |
| SicnuAlgorithmDialog | `sicnu_algorithm_dialog.{h,cpp}` | Processing toolbox algorithms via JobEngine |
