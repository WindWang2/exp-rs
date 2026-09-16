// terrain_dialog.h — Phase 11.2 Terrain Analysis Dialog
// (extended in terrain-hydrology-11 with hydrology, visibility and solar
// products routed to their domain operators)
#pragma once

#include "raster_processing_dialog_base.h"
#include <QFutureWatcher>

class QComboBox;
class QDoubleSpinBox;
class QLabel;

class TerrainDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit TerrainDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("terrain"); }
    QString dialogTitle() const override { return tr("Terrain Analysis"); }
    bool validateInputs() override;
    void onRun() override;

private slots:
    void onAnalysisFinished();

private:
    void setupUi();

    QComboBox *mLayerCombo = nullptr;
    QComboBox *mAnalysisCombo = nullptr;
    QDoubleSpinBox *mCellSizeSpin = nullptr;
    QDoubleSpinBox *mSunAzimuthSpin = nullptr;
    QDoubleSpinBox *mSunElevationSpin = nullptr;
    // Hydrology / visibility / solar parameter widgets (enabled per product).
    QDoubleSpinBox *mStreamThresholdSpin = nullptr;
    QDoubleSpinBox *mObserverColSpin = nullptr;
    QDoubleSpinBox *mObserverRowSpin = nullptr;
    QDoubleSpinBox *mObserverHeightSpin = nullptr;
    QDoubleSpinBox *mRadiusSpin = nullptr;
    QDoubleSpinBox *mDayOfYearSpin = nullptr;
    QDoubleSpinBox *mLatitudeSpin = nullptr;
    QLabel *mStatusLabel = nullptr;
    QFutureWatcher<bool> *mWatcher = nullptr;
};
