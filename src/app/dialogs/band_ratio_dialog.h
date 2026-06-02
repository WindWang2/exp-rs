// src/app/dialogs/band_ratio_dialog.h
#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QgsRasterLayer;

/**
 * Dialog for Band Ratio and IHS Transform operations.
 * Supports Band Ratio (band1/band2) and IHS Transform (R,G,B -> I,H,S)
 * using the ImageEnhancement algorithm library.
 */
class BandRatioDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BandRatioDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseOutput();
    void onRun();
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

    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;
};
