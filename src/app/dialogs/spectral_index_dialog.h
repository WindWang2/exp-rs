// src/app/dialogs/spectral_index_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QgsRasterLayer;

/**
 * Dialog for Spectral Index calculations.
 * Supports NDVI, EVI, SAVI, NDWI, NDBI, MNDWI indices
 * using the SpectralIndices algorithm library.
 */
class SpectralIndexDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit SpectralIndexDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("spectral_index"); }
    QString dialogTitle() const override { return tr("Spectral Index"); }
    void onRun() override;
    void cleanupRunResources() override;

private slots:
    void onIndexChanged(int index);

private:
    void setupUi();
    void populateBandCombos();
    void updateBandVisibility();

    QComboBox *m_indexCombo = nullptr;
    QComboBox *m_nirCombo = nullptr;
    QComboBox *m_redCombo = nullptr;
    QComboBox *m_greenCombo = nullptr;
    QComboBox *m_blueCombo = nullptr;
    QComboBox *m_swirCombo = nullptr;

    QLabel *m_nirLabel = nullptr;
    QLabel *m_redLabel = nullptr;
    QLabel *m_greenLabel = nullptr;
    QLabel *m_blueLabel = nullptr;
    QLabel *m_swirLabel = nullptr;

    QStringList m_tempFiles;
    std::vector<std::unique_ptr<QgsRasterLayer>> m_tempLayers;
};
