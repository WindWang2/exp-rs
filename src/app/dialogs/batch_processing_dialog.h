// src/app/dialogs/batch_processing_dialog.h
#pragma once

#include <QDialog>
#include <QList>

class QComboBox;
class QListWidget;
class QPushButton;
class QLineEdit;
class QLabel;
class QProgressBar;

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
    /// parameters) and single-input RS operators (declared defaults).
    /// Returns false and sets \p errorMessage on failure. No UI side effects.
    bool runBatchItem(const QString &algorithmId,
                      const QString &inputFile,
                      const QString &outputPath,
                      QString *errorMessage);

private slots:
    void onAddFiles();
    void onRemoveSelected();
    void onBrowseOutputDir();
    void onRun();
    void onAlgorithmChanged(int index);

private:
    void setupUi();
    void updateAlgorithmParameters();

    QComboBox *m_algorithmCombo = nullptr;
    QListWidget *m_fileList = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_runButton = nullptr;

    QStringList m_inputFiles;
    QString m_outputDir;
};
