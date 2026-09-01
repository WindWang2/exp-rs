// src/app/dialogs/band_math_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QLabel;
class QLineEdit;
class RasterLayerCombo;

/**
 * Dialog for Band Math operations.
 * Allows users to enter mathematical expressions and apply them
 * to multi-band raster layers using the BandMath algorithm.
 */
class BandMathDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit BandMathDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("band_math"); }
    QString dialogTitle() const override { return tr("波段运算"); }
    void onRun() override;

private slots:
    void onLayerChanged(int index);

private:
    void setupUi();
    void updateBandInfo();

    RasterLayerCombo *m_layerCombo = nullptr;
    QLabel *m_bandInfoLabel = nullptr;
    QLineEdit *m_expressionEdit = nullptr;
};
