// extract_band_dialog.h — Extract single band from multi-band raster
#pragma once

#include "raster_processing_dialog_base.h"

class BandRoleCombo;
class QLabel;
class RasterLayerCombo;

class ExtractBandDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit ExtractBandDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("extract_band"); }
    QString dialogTitle() const override { return tr("提取波段"); }
    bool validateInputs() override;
    void onRun() override;

private slots:
    void onLayerChanged(int index);
    void populateBandCombo();

private:
    void setupUi();

    RasterLayerCombo *m_layerCombo = nullptr;
    BandRoleCombo *m_bandCombo = nullptr;
    QLabel *m_infoLabel = nullptr;
};
