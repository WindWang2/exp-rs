// src/app/dialogs/spectral_index_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

namespace sicnu::data { class DataManager; }

class BandRoleCombo;
class QComboBox;
class QCheckBox;
class QLabel;
class QgsRasterLayer;

/**
 * Dialog for Spectral Index calculations.
 * Supports NDVI, EVI, SAVI, NDWI, NDBI, MNDWI via the rs:spectral_index operator.
 *
 * When a Data Manager is supplied (setDataManager), the input is picked as a
 * Data Asset. Without a Data Manager the dialog falls back to the raw-layer operator path.
 */
class SpectralIndexDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit SpectralIndexDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer) override;

    /// Supplies the Data Manager so the input can be picked as a Data Asset.
    /// Optional: when null, the dialog uses the raw-layer operator path.
    void setDataManager(sicnu::data::DataManager *dataManager);

protected:
    QString toolName() const override { return QStringLiteral("spectral_index"); }
    QString dialogTitle() const override { return tr("Spectral Index"); }
    void onRun() override;

private slots:
    void onIndexChanged(int index);
    void onInputAssetChanged(int comboIndex);

private:
    void setupUi();
    void populateBandCombos();
    void populateInputAssets();
    void updateBandVisibility();
    void runFromAsset();
    void runFromLayer();

    /// Band count for the currently selected input (asset snapshot or layer).
    int inputBandCount() const;

    /// Raster path of the currently selected input (asset source or layer).
    QString inputRasterPath() const;

    sicnu::data::DataManager *m_dataManager = nullptr;

    QComboBox *m_inputAssetCombo = nullptr;
    QLabel *m_inputAssetLabel = nullptr;

    QComboBox *m_indexCombo = nullptr;
    BandRoleCombo *m_nirCombo = nullptr;
    BandRoleCombo *m_redCombo = nullptr;
    BandRoleCombo *m_greenCombo = nullptr;
    BandRoleCombo *m_blueCombo = nullptr;
    BandRoleCombo *m_swirCombo = nullptr;

    QLabel *m_nirLabel = nullptr;
    QLabel *m_redLabel = nullptr;
    QLabel *m_greenLabel = nullptr;
    QLabel *m_blueLabel = nullptr;
    QLabel *m_swirLabel = nullptr;
};
