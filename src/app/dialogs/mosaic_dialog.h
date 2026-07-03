// src/app/dialogs/mosaic_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QListWidget;
class QPushButton;

/**
 * Dialog for mosaicking (stitching) multiple raster files into a single output.
 * Validates CRS consistency, computes the union extent, and merges overlapping regions.
 */
class MosaicDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit MosaicDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("mosaic"); }
    QString dialogTitle() const override { return tr("Mosaic"); }
    void onRun() override;

private slots:
    void addInputFile();
    void removeInputFile();

private:
    void setupUi();

    QListWidget *m_inputList = nullptr;
};
