// raster_processing_dialog_base.h — Base class for raster processing dialogs
#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QVariantMap>
#include <functional>

class QgsRasterLayer;
class AsyncGdalRunner;
class AsyncAlgorithmRunner;
class QgsProcessingAlgorithm;
class QgsProcessingContext;

/**
 * Base class for raster processing dialogs providing common UI elements and callbacks.
 *
 * Subclasses implement:
 *   - toolName() for log tag
 *   - dialogTitle() for message boxes
 *   - onRun() for the actual processing logic
 *
 * The base class provides:
 *   - Output file browse UI
 *   - Run/Cancel button bar
 *   - Input validation (output path, raster layer)
 *   - Default completion/failure handling
 */
class RasterProcessingDialogBase : public QDialog
{
    Q_OBJECT

public:
    explicit RasterProcessingDialogBase(QWidget *parent = nullptr);

    /**
     * Set the raster layer to process.
     */
    virtual void setRasterLayer(QgsRasterLayer *layer);

    /**
     * Get the output file path.
     */
    QString outputPath() const;

    /**
     * True while a GDAL task is running (Run button disabled).
     */
    bool isRunning() const { return m_running; }

    /**
     * Disable Run and mark dialog as busy. Called automatically by runGdalTask().
     */
    void startRun();

    /**
     * Re-enable Run after completion or failure.
     */
    void finishRun();

    /**
     * Run a GDAL I/O task on a background thread.
     * Connects to onCompleted/onFailed automatically on first use.
     */
    void runGdalTask(const std::function<QString()> &task);

    /**
     * Run a QGIS Processing algorithm on a background thread.
     * Connects to onCompleted/onFailed automatically on first use.
     */
    void runAlgorithmTask(const QgsProcessingAlgorithm *algorithm,
                          const QVariantMap &parameters,
                          QgsProcessingContext &context);

protected:
    // --- Virtual hooks for subclasses ---

    /**
     * Return the tool name for log messages (e.g., "band_ratio", "contrast_stretch").
     */
    virtual QString toolName() const = 0;

    /**
     * Return the dialog title for message boxes.
     */
    virtual QString dialogTitle() const = 0;

    /**
     * Validate inputs before running. Return true if valid.
     * Default implementation checks output path and raster layer.
     */
    virtual bool validateInputs();

    /**
     * Execute the processing. Called when Run is clicked and inputs are valid.
     */
    virtual void onRun() = 0;

    /**
     * Hook for subclasses to release resources held only for the duration of a run.
     */
    virtual void cleanupRunResources() {}

    // --- UI helpers ---

    /**
     * Create the output file row (label + line edit + browse button).
     */
    void setupOutputRow(QVBoxLayout *layout);

    /**
     * Create the button bar (Run + Cancel).
     */
    void setupButtonBar(QVBoxLayout *layout);

    /**
     * Default browse output implementation (GeoTIFF filter).
     */
    void browseOutput();

    /**
     * Default completion handler (enable button, log success, accept).
     */
    void handleCompleted(const QString &outputPath);

    /**
     * Default failure handler (enable button, log error, show message).
     */
    void handleFailed(const QString &error);

public slots:
    /**
     * Slot for async runner completed signal.
     * Can be connected directly to AsyncGdalRunner::completed or AsyncAlgorithmRunner::completed.
     */
    void onCompleted(const QString &outputPath);

    /**
     * Slot for async runner failed signal.
     * Can be connected directly to AsyncGdalRunner::failed or AsyncAlgorithmRunner::failed.
     */
    void onFailed(const QString &errorMessage);

protected:
    // --- Members ---
    QgsRasterLayer *m_rasterLayer = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    AsyncGdalRunner *m_runner = nullptr;
    AsyncAlgorithmRunner *m_algorithmRunner = nullptr;
    bool m_running = false;
};
