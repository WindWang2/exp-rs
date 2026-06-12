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
