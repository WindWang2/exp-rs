// terrain_dialog.h — Phase 11.2 Terrain Analysis Dialog
#pragma once

#include <QDialog>
#include <QFutureWatcher>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QgsRasterLayer;

class TerrainDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TerrainDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);
    QString outputPath() const { return mOutputPath; }

private slots:
    void runAnalysis();
    void onAnalysisFinished();
    void onBrowseOutput();

private:
    QComboBox *mLayerCombo = nullptr;
    QComboBox *mAnalysisCombo = nullptr;
    QDoubleSpinBox *mCellSizeSpin = nullptr;
    QDoubleSpinBox *mSunAzimuthSpin = nullptr;
    QDoubleSpinBox *mSunElevationSpin = nullptr;
    QLineEdit *mOutputEdit = nullptr;
    QLabel *mStatusLabel = nullptr;
    QPushButton *mRunButton = nullptr;
    QFutureWatcher<bool> *mWatcher = nullptr;
    QString mOutputPath;
};
