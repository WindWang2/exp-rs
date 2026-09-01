// src/app/dialogs/batch_processing_dialog.h
#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QVariantMap>

#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QFrame;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;
class QProgressBar;
class QSpinBox;

class QgsAbstractProcessingParameterWidgetWrapper;
class QgsProcessingAlgorithm;
class QgsProcessingContext;

namespace sicnu::processing
{
struct AlgorithmDescriptor;
}

namespace sicnu
{
struct AlgorithmTaskInfo;
}

/**
 * Dialog for batch processing - running the same algorithm on multiple files.
 */
class BatchProcessingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BatchProcessingDialog(QWidget *parent = nullptr);
    ~BatchProcessingDialog() override;

    void setAlgorithmId(const QString &algorithmId);

    /// Programmatic input-file injection (also used by tests).
    void setInputFiles(const QStringList &files);

    /// Programmatic output-directory injection (also used by tests).
    void setOutputDir(const QString &dir);

    /// Runs one batch item: executes \p algorithmId on \p inputFile writing
    /// \p outputPath. Supports QGIS provider algorithms (INPUT/OUTPUT
    /// parameters) and single-input RS operators (declared defaults; values
    /// in \p paramOverrides win over defaults, but the main input and output
    /// are always derived from \p inputFile / \p outputPath). No UI side
    /// effects. Returns false and sets \p errorMessage on failure.
    bool runBatchItem(const QString &algorithmId,
                      const QString &inputFile,
                      const QString &outputPath,
                      QString *errorMessage,
                      const QVariantMap &paramOverrides = QVariantMap());

    /// Collects current parameter-form values as an override map (used by the
    /// run path and by tests to verify the form state).
    QVariantMap collectParamOverrides() const;

private slots:
    void onAddFiles();
    void onRemoveSelected();
    void onBrowseOutputDir();
    void onRun();
    void onAlgorithmChanged(int index);
    /// Advances the async batch: reacts to the current batch task's terminal
    /// transition (TaskCenter::taskUpdated) and chains the next item.
    void onBatchTaskUpdated(const sicnu::AlgorithmTaskInfo &info);


protected:
    /// A mid-run close would silently abandon the chain while the in-flight
    /// item keeps running and auto-loading (#704): block Esc/X while running
    /// — the user cancels via the Cancel button first.
    void reject() override;

private:
    void setupUi();
    void updateAlgorithmParameters();
    /// (Re)builds the parameter form for an RS operator.
    void rebuildParamForm(const sicnu::processing::AlgorithmDescriptor &desc);
    /// (Re)builds the parameter form for a QGIS provider algorithm using the
    /// QGIS parameter-widget registry (input/output params excluded — they are
    /// decided by the batch item).
    void rebuildQgisParamForm(const QgsProcessingAlgorithm *alg);
    /// Drops every widget in the parameter form.
    void clearParamForm();
    /// Submits batch item \a m_batchIndex as a TaskCenter job (executor wraps
    /// runBatchItem) and returns; completion continues in onBatchTaskUpdated.
    void submitNextBatchItem();
    /// Restores the dialog state after the last item and reports the summary.
    void finishBatch();
    /// Unique output path for the batch item (collision suffix policy).
    QString uniqueOutputPath(const QString &inputFile) const;

    QComboBox *m_algorithmCombo = nullptr;
    QListWidget *m_fileList = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_addFilesBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_browseBtn = nullptr;

    /// "参数覆盖" section: rebuilt per algorithm (RS operators get typed
    /// editors, QGIS algorithms get QGIS parameter-widget wrappers).
    QGroupBox *m_paramFrame = nullptr;
    QFormLayout *m_paramForm = nullptr;
    /// Parameter-name → editor widget for the current RS operator.
    QHash<QString, QWidget *> m_paramWidgets;
    /// QGIS parameter wrappers for the current provider algorithm.
    QVector<QgsAbstractProcessingParameterWidgetWrapper *> m_qgisWrappers;
    /// Processing context backing the QGIS parameter widgets.
    std::unique_ptr<QgsProcessingContext> m_qgisContext;

    bool m_isRunning = false;
    bool m_canceled = false;

    // Async batch state (execution-plane goal: items run as TaskCenter jobs
    // instead of a processEvents loop on the GUI thread, which starved user
    // input — the Cancel button could not fire between items).
    QStringList m_batchFiles;
    int m_batchIndex = 0;
    int m_batchSuccess = 0;
    int m_batchFail = 0;
    QStringList m_batchErrors;
    QString m_batchAlgorithmId;
    QVariantMap m_batchOverrides;
    QString m_batchOutputExt = QStringLiteral( ".tif" );
    long m_batchTaskId = -1;

    QStringList m_inputFiles;
    QString m_outputDir;
};
