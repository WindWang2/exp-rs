// src/app/dialogs/band_ratio_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QLabel;

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

protected:
    QString toolName() const override { return QStringLiteral("band_ratio"); }
    QString dialogTitle() const override { return tr("Band Ratio / IHS"); }
    void onRun() override;

private slots:
    void onModeChanged(int index);

private:
    void setupUi();
    void populateBandCombos();

    QComboBox *m_modeCombo = nullptr;

    // Band Ratio controls
    QLabel *m_band1Label = nullptr;
    QComboBox *m_band1Combo = nullptr;
    QLabel *m_band2Label = nullptr;
    QComboBox *m_band2Combo = nullptr;

    // IHS controls
    QLabel *m_redLabel = nullptr;
    QComboBox *m_redCombo = nullptr;
    QLabel *m_greenLabel = nullptr;
    QComboBox *m_greenCombo = nullptr;
    QLabel *m_blueLabel = nullptr;
    QComboBox *m_blueCombo = nullptr;


};
