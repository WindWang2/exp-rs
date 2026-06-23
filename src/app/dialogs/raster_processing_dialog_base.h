// raster_processing_dialog_base.h — Base class for raster processing dialogs
#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

class QgsRasterLayer;

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
    void setRasterLayer(QgsRasterLayer *layer);

    /**
     * Get the output file path.
     */
    QString outputPath() const;

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

    // --- Members ---
    QgsRasterLayer *m_rasterLayer = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
};
