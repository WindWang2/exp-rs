// src/app/dialogs/orthorectification_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

#include <json/json.h>
#include <QtGlobal>

class CrsSelector;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

/**
 * Professional orthorectification dialog over the gdal:orthorectification
 * operator: RPC/GCP input, target CRS, optional DEM, resampling, output
 * resolution, constant-elevation fallback, and output NoData.
 *
 * The dialog is a presentation adapter: buildParams() assembles the operator
 * parameter JSON (public and testable headlessly); onRun submits it through
 * the shared runOperatorTask seam.
 */
class OrthorectificationDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit OrthorectificationDialog(QWidget *parent = nullptr);
    void setRasterLayer(QgsRasterLayer *layer) override;

    /// Builds the gdal:orthorectification parameter JSON from the current
    /// widget state. Public for headless testing.
    Json::Value buildParams() const;

protected:
    QString toolName() const override { return QStringLiteral("orthorectification"); }
    QString dialogTitle() const override { return tr("Orthorectification"); }
    void onRun() override;

private slots:
    void onBrowseDem();

private:
    void setupUi();
    /// Refreshes the model-status label (RPC / GCP / none) for the input.
    /// The RPC/GCP probe runs on the bounded scan pool (#1805 F7); this only
    /// arms the generation and shows the transient checking state.
    void refreshModelStatus();
    /// GUI-thread application of a probe result (guarded by generation).
    void applyModelStatus( int model );

    CrsSelector *m_targetCrsEdit = nullptr;
    QLineEdit *m_demEdit = nullptr;
    QPushButton *m_demBrowseButton = nullptr;
    QComboBox *m_resamplingCombo = nullptr;
    QDoubleSpinBox *m_resolutionSpin = nullptr;
    QDoubleSpinBox *m_heightSpin = nullptr;
    QCheckBox *m_nodataCheck = nullptr;
    QDoubleSpinBox *m_nodataSpin = nullptr;
    QLabel *m_modelStatusLabel = nullptr;
    /// Scan-pool generation of the in-flight RPC/GCP probe (0 = none).
    quint64 m_probeGeneration = 0;
};
