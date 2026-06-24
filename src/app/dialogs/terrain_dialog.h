// terrain_dialog.h — Phase 11.2 Terrain Analysis Dialog
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
    QLabel *mStatusLabel = nullptr;
    QFutureWatcher<bool> *mWatcher = nullptr;
};
