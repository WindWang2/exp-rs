// src/app/dialogs/spatial_filter_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;

/**
 * Dialog for Spatial Filtering operations.
 * Supports Mean, Gaussian, Median, Sobel, and Laplacian filters
 * using the ImageEnhancement algorithm library.
 */
class SpatialFilterDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit SpatialFilterDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("spatial_filter"); }
    QString dialogTitle() const override { return tr("Spatial Filter"); }
    void onRun() override;

private slots:

private:
    void setupUi();

    QComboBox *m_filterTypeCombo = nullptr;
    QComboBox *m_kernelSizeCombo = nullptr;
};
