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

    QComboBox *m_algorithmCombo = nullptr;
    QListWidget *m_fileList = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_runButton = nullptr;

    /// "参数覆盖" section: rebuilt per algorithm (RS operators get typed
    /// editors, QGIS algorithms get QGIS parameter-widget wrappers).
    QFrame *m_paramFrame = nullptr;
    QFormLayout *m_paramForm = nullptr;
    /// Parameter-name → editor widget for the current RS operator.
    QHash<QString, QWidget *> m_paramWidgets;
    /// QGIS parameter wrappers for the current provider algorithm.
    QVector<QgsAbstractProcessingParameterWidgetWrapper *> m_qgisWrappers;
    /// Processing context backing the QGIS parameter widgets.
    std::unique_ptr<QgsProcessingContext> m_qgisContext;

    QStringList m_inputFiles;
    QString m_outputDir;
};
