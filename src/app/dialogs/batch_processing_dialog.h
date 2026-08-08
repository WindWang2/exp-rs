// src/app/dialogs/batch_processing_dialog.h
#pragma once

#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QStringList>

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
                      const QJsonObject &paramOverrides = QJsonObject());

    /// Collects current RS parameter-form values as an override object
    /// (used by the run path and by tests to verify the form state).
    QJsonObject collectParamOverrides() const;

private slots:
    void onAddFiles();
    void onRemoveSelected();
    void onBrowseOutputDir();
    void onRun();
    void onAlgorithmChanged(int index);

private:
    void setupUi();
    void updateAlgorithmParameters();
    /// (Re)builds the parameter form for an RS operator; hides the section
    /// for non-RS algorithms.
    void rebuildParamForm(const sicnu::processing::AlgorithmDescriptor &desc);

    QComboBox *m_algorithmCombo = nullptr;
    QListWidget *m_fileList = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_runButton = nullptr;

    /// "RS 参数" section: hidden for QGIS algorithms, rebuilt per RS operator.
    QFrame *m_paramFrame = nullptr;
    QFormLayout *m_paramForm = nullptr;
    /// Parameter-name → editor widget for the current RS operator.
    QHash<QString, QWidget *> m_paramWidgets;

    QStringList m_inputFiles;
    QString m_outputDir;
};
