// raster_processing_dialog_base.h — Base class for raster processing dialogs
#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSize>
#include <QString>
#include <QVBoxLayout>
#include <QVariantMap>
#include <functional>

#include <json/json.h>

#include "shell/gui_job_adapter.h"

class QgsRasterLayer;
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
 *   - Output file browse UI with standardized QGroupBox ("输出配置")
 *   - Standardized QDialogButtonBox button bar (Help, Reset, Cancel, Run)
 *   - Dynamic High-DPI responsive minimumSizeHint
 *   - Input validation (output path, raster layer)
 *   - Default completion/failure handling
 */
class RasterProcessingDialogBase : public QDialog
{
    Q_OBJECT

public:
    explicit RasterProcessingDialogBase(QWidget *parent = nullptr);

    /**
     * Minimum size hint calculating responsive dimensions for High-DPI displays.
     */
    QSize minimumSizeHint() const override;

    /**
     * Preferred size hint calculating responsive dimensions for High-DPI displays.
     */
    QSize sizeHint() const override;

    /**
     * Ignore reject/close while a background task is running.
     */
    void reject() override;

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
    bool isRunning() const { return m_running || m_jobHandle.isRunning(); }

    /**
     * String marker prefix for structured error returns from runGdalTask.
     */
    static QString gdalErrorMarker() { return QStringLiteral( "\x01SICNU_ERR\x01" ); }

    /**
     * Disable Run and mark dialog as busy. Called automatically by runGdalTask().
     */
    void startRun();

    /**
     * Re-enable Run after completion or failure.
     */
    void finishRun();

    /**
     * Run a GDAL I/O lambda through Task Center (callable:gdal_task).
     * Task return contract (legacy AsyncGdalRunner):
     *   - non-empty path → success
     *   - empty → generic failure
     *   - "\x01SICNU_ERR\x01" + message → structured failure
     * Completes via onCompleted/onFailed on the GUI thread.
     */
    void runGdalTask(const std::function<QString()> &task);

    /**
     * Run a QGIS Processing algorithm through Task Center (processing: prefix id).
     * Completes via onCompleted/onFailed on the GUI thread.
     * Prefer SicnuAlgorithmDialog for full toolbox parameter UIs.
     */
    void runAlgorithmTask(const QgsProcessingAlgorithm *algorithm,
                          const QVariantMap &parameters,
                          QgsProcessingContext &context);

    /**
     * Run an RSOperator through Task Center (registry / prefix path).
     *
     * Prefer this over direct algorithm calls so GUI, CLI, and MCP share one
     * execution path. Parameters must match the operator schema(); the result
     * must contain an "output" string path.
     */
    void runOperatorTask(const QString &operatorId, const Json::Value &params);

    /**
     * Variant that also delivers the operator's JSON result (e.g. transition
     * matrices, statistics) via @p onResult on the GUI thread, after the
     * standard completion handling. Useful for dialogs that surface quality
     * metrics (change percentages, per-class totals) next to the output.
     */
    void runOperatorTask(const QString &operatorId, const Json::Value &params,
                         const std::function<void(const Json::Value &result)> &onResult);

    /**
     * Whether the dialog should auto-accept (close) after a successful run.
     * Dialogs that surface result summaries (transition matrices, statistics)
     * can configure this or return false so the user can read the metrics before dismissing.
     */
    virtual bool shouldAutoAcceptOnSuccess() const { return m_autoAcceptOnSuccess; }

    /**
     * Configure auto-accept behavior on success.
     */
    void setShouldAutoAcceptOnSuccess(bool autoAccept) { m_autoAcceptOnSuccess = autoAccept; }

    /**
     * Access to the dialog button box.
     */
    QDialogButtonBox *buttonBox() const { return m_buttonBox; }

    /**
     * Access to individual buttons in the button box.
     */
    QPushButton *runButton() const { return m_runButton; }
    QPushButton *cancelButton() const { return m_cancelButton; }
    QPushButton *resetButton() const { return m_resetButton; }
    QPushButton *helpButton() const { return m_helpButton; }

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
     * Create standard input source group container (titled "输入数据").
     */
    QGroupBox *setupInputGroup(QVBoxLayout *layout, const QString &title = QString());

    /**
     * Create standard algorithm parameter group container (titled "算法参数").
     */
    QGroupBox *setupParamGroup(QVBoxLayout *layout, const QString &title = QString());

    /**
     * Create the standardized output group (titled "输出配置", containing output path edit and browse button).
     */
    QGroupBox *setupOutputGroup(QVBoxLayout *layout, const QString &title = QString());

    /**
     * Create standard advanced options group container (titled "高级选项").
     */
    QGroupBox *setupAdvancedGroup(QVBoxLayout *layout, const QString &title = QString());

    /**
     * Legacy alias for setupOutputGroup to maintain subclass compatibility.
     */
    void setupOutputRow(QVBoxLayout *layout);

    /**
     * Create the button bar using standard QDialogButtonBox (Help, Reset, Cancel, Run/OK).
     */
    void setupButtonBar(QVBoxLayout *layout);

    /**
     * Insert a collapsible help banner (short description) at the top of \a layout.
     * Call after setWindowTitle / once main layout exists.
     */
    void setupHelpBanner( QVBoxLayout *layout );

    /**
     * Default browse output implementation (GeoTIFF filter).
     */
    void browseOutput();

    /**
     * HTML help body for the Help button (override for custom text).
     * Default looks up SicnuDialogHelp by toolName().
     */
    virtual QString dialogHelpHtml() const;

    /**
     * Default completion handler (enable button, log success, accept).
     */
    void handleCompleted(const QString &outputPath);

    /**
     * Default failure handler (enable button, log error, show message).
     */
    void handleFailed(const QString &error);

protected slots:
    /**
     * Slot invoked when the Run / OK button is clicked or Enter is pressed.
     */
    virtual void onRunClicked();

    /**
     * Slot invoked when the Reset button is clicked. Subclasses can override to restore defaults.
     */
    virtual void onResetClicked();

    /**
     * Slot invoked when the Help button is clicked.
     */
    virtual void onHelpClicked();

public slots:
    /**
     * Slot for async runner completed signal (JobEngine operator/callable path).
     */
    void onCompleted(const QString &outputPath);

    /**
     * Slot for async runner failed signal.
     */
    void onFailed(const QString &errorMessage);

protected:
    // --- Members ---
    QgsRasterLayer *m_rasterLayer = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_helpButton = nullptr;
    bool m_running = false;
    bool m_autoAcceptOnSuccess = true;
    sicnu::app::GuiJobHandle m_jobHandle{ this };
};
