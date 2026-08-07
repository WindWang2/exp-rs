// apply_mask_dialog.h — Apply Mask Dialog
#pragma once

#include "raster_processing_dialog_base.h"

#include <json/json.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

/**
 * Applies a binary QA mask (1 = masked, 0 = clear) to a product raster:
 * obscured pixels become NoData in every band, producing analysis-ready
 * imagery. Presentation adapter over rs:apply_mask — buildParams() assembles
 * the operator JSON from widget state; onRun() submits it through the shared
 * Task Center path (same execution seam as GUI/CLI/Agent).
 */
class ApplyMaskDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit ApplyMaskDialog(QWidget *parent = nullptr);

    void populateLayers();
    void setRasterLayer( QgsRasterLayer *layer ) override;

    /// Assemble the rs:apply_mask operator JSON from the current widget state.
    Json::Value buildParams() const;

protected:
    QString toolName() const override { return QStringLiteral("apply_mask"); }
    QString dialogTitle() const override { return tr("应用掩膜"); }
    bool validateInputs() override;
    void onRun() override;

private slots:
    void onInputLayerChanged();

private:
    void setupUi();
    /// Preselect @a layer (when present in the project) as the input raster.
    void preselectInputLayer( QgsRasterLayer *layer );

    QComboBox *m_inputLayerCombo = nullptr;
    QComboBox *m_maskLayerCombo = nullptr;
    QCheckBox *m_useNoDataCheck = nullptr;
    QDoubleSpinBox *m_noDataSpin = nullptr;
    QCheckBox *m_alignMaskCheck = nullptr;
    /// True once the user edited the output path; disables auto-suggestion.
    bool m_outputTouched = false;
};
