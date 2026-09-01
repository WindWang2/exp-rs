// src/app/dialogs/contrast_stretch_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class HistogramStretchWidget;
class RasterLayerCombo;

/**
 * Dialog for Photoshop-style Interactive Contrast Stretch and Levels Adjustment.
 * Supports Multi-channel RGB/Grayscale interactive histograms, Level cut-off handles,
 * live map canvas rendering, and async TaskCenter GeoTIFF processing.
 */
class ContrastStretchDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit ContrastStretchDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("contrast_stretch"); }
    QString dialogTitle() const override { return tr("对比度拉伸与色阶调节"); }
    void onRun() override;

private slots:
    void onLayerChanged(int index);
    void onMethodChanged(int index);

private:
    void setupUi();

    RasterLayerCombo *m_layerCombo = nullptr;
    HistogramStretchWidget *m_stretchWidget = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QDoubleSpinBox *m_clipSpin = nullptr;
    QDoubleSpinBox *m_stddevSpin = nullptr;
    QLabel *m_clipLabel = nullptr;
    QLabel *m_stddevLabel = nullptr;
};
