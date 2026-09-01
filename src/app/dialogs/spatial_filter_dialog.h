// src/app/dialogs/spatial_filter_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class RasterLayerCombo;

/**
 * Dialog for Spatial Filtering operations.
 * All filters (Mean/Gaussian/Median/Sobel/Laplacian) use OpenCV RSOperators.
 */
class SpatialFilterDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit SpatialFilterDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("spatial_filter"); }
    QString dialogTitle() const override { return tr("空间滤波"); }
    void onRun() override;

private slots:
    void onFilterTypeChanged(int index);
    void onLayerChanged(int index);

private:
    void setupUi();

    RasterLayerCombo *m_layerCombo = nullptr;
    QComboBox *m_filterTypeCombo = nullptr;
    QComboBox *m_kernelSizeCombo = nullptr;
    QLabel *m_sigmaLabel = nullptr;
    QDoubleSpinBox *m_sigmaSpin = nullptr;
};
