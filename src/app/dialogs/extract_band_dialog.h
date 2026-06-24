// extract_band_dialog.h — Extract single band from multi-band raster
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QLabel;
class AsyncGdalRunner;

class ExtractBandDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit ExtractBandDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("extract_band"); }
    QString dialogTitle() const override { return tr("Extract Band"); }
    void onRun() override;

private slots:
    void onLayerChanged();
    void populateBandCombo();

private:
    void setupUi();

    QComboBox *m_layerCombo = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QLabel *m_infoLabel = nullptr;
    AsyncGdalRunner *m_runner = nullptr;
};
