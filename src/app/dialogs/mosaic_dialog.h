// src/app/dialogs/mosaic_dialog.h
#pragma once

#include <QDialog>

class QListWidget;
class QLineEdit;
class QPushButton;
class AsyncGdalRunner;

/**
 * Dialog for mosaicking (stitching) multiple raster files into a single output.
 * Validates CRS consistency, computes the union extent, and merges overlapping regions.
 */
class MosaicDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MosaicDialog(QWidget *parent = nullptr);

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
    AsyncGdalRunner *m_runner = nullptr;
};
