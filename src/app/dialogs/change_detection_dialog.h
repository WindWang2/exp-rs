// change_detection_dialog.h — Change Detection Dialog
#pragma once

#include "raster_processing_dialog_base.h"

#include <json/json.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QSpinBox;
class RasterLayerCombo;
class QLabel;


class ChangeDetectionDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit ChangeDetectionDialog(QWidget *parent = nullptr);

    void populateLayers();

    /// Assembles the rs:change_detection operator JSON from the current widget
    /// state (defaults omitted, explicit options surfaced).
    Json::Value buildParams() const;

protected:
    QString toolName() const override { return QStringLiteral("change_detection"); }
    QString dialogTitle() const override { return tr("Change Detection"); }
    bool validateInputs() override;
    void onRun() override;

private slots:
    void updateBandSelectors();
    void onMethodChanged(int index);
    void onMakeMaskToggled();
    void onThresholdMethodChanged(int index);
    void openComparisonPreview();

private:
    void setupUi();
    /// Shows/hides the mask-parameter section and the strategy-specific spins.
    void updateMaskParamVisibility();

    RasterLayerCombo *m_beforeLayerCombo = nullptr;
    RasterLayerCombo *m_afterLayerCombo = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QComboBox *m_beforeBandCombo = nullptr;
    QComboBox *m_afterBandCombo = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
    QLabel *m_thresholdLabel = nullptr;
    QLabel *m_statusLabel = nullptr;

    // Mask-output section (makeMask) with threshold strategy / cleanup / MMU.
    QCheckBox *m_makeMaskCheck = nullptr;
    QFrame *m_maskParamFrame = nullptr;
    QComboBox *m_thresholdMethodCombo = nullptr;
    QDoubleSpinBox *m_percentileSpin = nullptr;
    QDoubleSpinBox *m_statisticalKSpin = nullptr;
    QComboBox *m_cleanupCombo = nullptr;
    QSpinBox *m_cleanupIterSpin = nullptr;
    QSpinBox *m_minAreaSpin = nullptr;

};
