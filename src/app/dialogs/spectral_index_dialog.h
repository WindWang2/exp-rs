// src/app/dialogs/spectral_index_dialog.h
#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;
class QgsRasterLayer;

/**
 * Dialog for Spectral Index calculations.
 * Supports NDVI, EVI, SAVI, NDWI, NDBI, MNDWI indices
 * using the SpectralIndices algorithm library.
 */
class SpectralIndexDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SpectralIndexDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseOutput();
    void onRun();
    void onIndexChanged(int index);

private:
    void setupUi();

    QComboBox *m_indexCombo = nullptr;
    QComboBox *m_nirCombo = nullptr;
    QComboBox *m_redCombo = nullptr;
    QComboBox *m_greenCombo = nullptr;
    QComboBox *m_blueCombo = nullptr;
    QComboBox *m_swirCombo = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;
};
