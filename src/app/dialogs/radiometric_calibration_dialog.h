// src/app/dialogs/radiometric_calibration_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class RasterLayerCombo;

/**
 * Radiometric calibration dialog: converts DN to radiance / TOA reflectance /
 * brightness temperature via the rs:radiometric_calibration operator.
 *
 * The sensor metadata file (Landsat *_MTL.txt / Sentinel-2 MTD_MSI*.xml) is
 * auto-detected next to the input raster when present; the dialog previews
 * the resolved coefficients (spacecraft, processing level, sun elevation,
 * band count) before running.
 */
class RadiometricCalibrationDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit RadiometricCalibrationDialog(QWidget *parent = nullptr);
    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("radiometric_calibration"); }
    QString dialogTitle() const override { return tr("辐射定标"); }
    void onRun() override;

private slots:
    void onLayerChanged(int index);
    void onBrowseMetadata();
    void onAllBandsToggled(bool checked);

private:
    void setupUi();
    void populateBandCombo();
    /// Resolves the metadata path (explicit or auto-detected sibling) and
    /// refreshes the metadata status label.
    void refreshMetadataStatus();

    QString resolvedMetadataPath() const;

    RasterLayerCombo *m_layerCombo = nullptr;
    QComboBox *m_unitCombo = nullptr;
    QCheckBox *m_allBandsCheck = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QLabel *m_bandLabel = nullptr;
    QLineEdit *m_metadataEdit = nullptr;
    QPushButton *m_metadataBrowseButton = nullptr;
    QLabel *m_metadataStatusLabel = nullptr;
};
