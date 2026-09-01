// src/app/dialogs/band_ratio_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class BandRoleCombo;
class QComboBox;
class QLabel;
class RasterLayerCombo;

/**
 * Dialog for Band Ratio and IHS Transform operations.
 * Supports Band Ratio (band1/band2) and IHS Transform (R,G,B -> I,H,S)
 * using the ImageEnhancement algorithm library.
 */
class BandRatioDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit BandRatioDialog(QWidget *parent = nullptr);
    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("band_ratio"); }
    QString dialogTitle() const override { return tr("波段比值与 IHS 变换"); }
    void onRun() override;

private slots:
    void onLayerChanged(int index);
    void onModeChanged(int index);

private:
    void setupUi();
    void populateBandCombos();

    RasterLayerCombo *m_layerCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;

    // Band Ratio controls
    QLabel *m_band1Label = nullptr;
    BandRoleCombo *m_band1Combo = nullptr;
    QLabel *m_band2Label = nullptr;
    BandRoleCombo *m_band2Combo = nullptr;

    // IHS controls
    QLabel *m_redLabel = nullptr;
    BandRoleCombo *m_redCombo = nullptr;
    QLabel *m_greenLabel = nullptr;
    BandRoleCombo *m_greenCombo = nullptr;
    QLabel *m_blueLabel = nullptr;
    BandRoleCombo *m_blueCombo = nullptr;
};
