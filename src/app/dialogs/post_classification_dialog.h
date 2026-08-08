// post_classification_dialog.h — Post-Classification Comparison dialog
#pragma once

#include "raster_processing_dialog_base.h"

#include <json/json.h>

class QComboBox;
class QLabel;
class QSpinBox;
class RasterLayerCombo;

/**
 * Post-classification comparison workbench dialog: compares two thematic
 * rasters and reports the per-class transition matrix, gains/losses, and a
 * change-type map via the rs:post_classification_change operator (ADR 0089).
 * Presentation adapter — buildParams() assembles the operator JSON from
 * widget state; onRun() submits it through the shared Task Center path.
 */
class PostClassificationDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit PostClassificationDialog(QWidget *parent = nullptr);

    void populateLayers();

    /// Assemble the rs:post_classification_change operator JSON.
    Json::Value buildParams() const;

protected:
    QString toolName() const override { return QStringLiteral("post_classification_change"); }
    QString dialogTitle() const override { return tr("后分类比较"); }
    bool validateInputs() override;
    void onRun() override;

private:
    void setupUi();
    void populateBandCombo( RasterLayerCombo *layerCombo, QComboBox *bandCombo );
    /// Render the operator's transition-matrix / changed-percent result.
    void showResultSummary( const Json::Value &result );

    RasterLayerCombo *m_beforeLayerCombo = nullptr;
    RasterLayerCombo *m_afterLayerCombo = nullptr;
    QComboBox *m_beforeBandCombo = nullptr;
    QComboBox *m_afterBandCombo = nullptr;
    QSpinBox *m_classCountSpin = nullptr;
    QLabel *m_summaryLabel = nullptr;
};
