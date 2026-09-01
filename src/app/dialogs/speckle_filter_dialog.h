// src/app/dialogs/speckle_filter_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class RasterLayerCombo;

/**
 * Dialog for SAR Speckle Filtering operations.
 * Supports Lee, Frost, Kuan, and Gamma-MAP filters
 * for reducing speckle noise in synthetic aperture radar imagery.
 */
class SpeckleFilterDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit SpeckleFilterDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("speckle_filter"); }
    QString dialogTitle() const override { return tr("SAR 斑点滤波"); }
    void onRun() override;

private slots:
    void onLayerChanged(int index);
    void onFilterTypeChanged(int index);

private:
    void setupUi();

    RasterLayerCombo *m_layerCombo = nullptr;
    QComboBox *m_filterTypeCombo = nullptr;
    QComboBox *m_kernelSizeCombo = nullptr;
    QDoubleSpinBox *m_noiseVarSpin = nullptr;
    QDoubleSpinBox *m_dampingSpin = nullptr;
    QLabel *m_noiseVarLabel = nullptr;
    QLabel *m_dampingLabel = nullptr;
};
