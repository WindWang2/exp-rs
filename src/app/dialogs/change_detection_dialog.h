// change_detection_dialog.h — Change Detection Dialog
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QDoubleSpinBox;
class RasterLayerCombo;
class QLabel;


class ChangeDetectionDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit ChangeDetectionDialog(QWidget *parent = nullptr);

    void populateLayers();

protected:
    QString toolName() const override { return QStringLiteral("change_detection"); }
    QString dialogTitle() const override { return tr("Change Detection"); }
    bool validateInputs() override;
    void onRun() override;

private slots:
    void updateBandSelectors();
    void onMethodChanged(int index);
    void openComparisonPreview();

private:
    void setupUi();

    RasterLayerCombo *m_beforeLayerCombo = nullptr;
    RasterLayerCombo *m_afterLayerCombo = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QComboBox *m_beforeBandCombo = nullptr;
    QComboBox *m_afterBandCombo = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
    QLabel *m_thresholdLabel = nullptr;
    QLabel *m_statusLabel = nullptr;

};
