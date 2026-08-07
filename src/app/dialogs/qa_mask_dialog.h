// src/app/dialogs/qa_mask_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QLabel;
class QSpinBox;

/**
 * Dialog for deriving a cloud / cloud-shadow / snow mask from a product QA
 * band (Landsat QA_PIXEL, Sentinel-2 SCL, or a generic bitmask) via the
 * rs:qa_mask operator.
 */
class QaMaskDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit QaMaskDialog(QWidget *parent = nullptr);
    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("qa_mask"); }
    QString dialogTitle() const override { return tr("QA 掩膜"); }
    void onRun() override;

private slots:
    void onSourceChanged(int index);

private:
    void setupUi();
    void populateBandCombo();

    QComboBox *m_sourceCombo = nullptr;
    QComboBox *m_maskCombo = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QSpinBox *m_bitsSpin = nullptr;
    QLabel *m_bitsLabel = nullptr;
};
