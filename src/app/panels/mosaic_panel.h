// src/app/panels/mosaic_panel.h
#pragma once

#include <QWidget>

class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;
class AsyncGdalRunner;

/**
 * Panel for mosaicking (stitching) multiple raster files into a single output.
 * Can be embedded in the main window or used as a standalone panel.
 */
class MosaicPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MosaicPanel(QWidget *parent = nullptr);

    /**
     * Get the output file path.
     */
    QString outputPath() const;

    /**
     * Get the list of input files.
     */
    QStringList inputFiles() const;

signals:
    /**
     * Emitted when the mosaic operation completes successfully.
     */
    void mosaicCompleted(const QString &outputPath);

    /**
     * Emitted when the mosaic operation fails.
     */
    void mosaicFailed(const QString &error);

    /**
     * Emitted when progress updates are available.
     */
    void progressChanged(int progress);

private slots:
    void addInputFile();
    void removeInputFile();
    void browseOutput();
    void runMosaic();
    void onCompleted(const QString &outputPath);
    void onFailed(const QString &error);

private:
    void setupUi();

    QListWidget *m_inputList = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    AsyncGdalRunner *m_runner = nullptr;
};
