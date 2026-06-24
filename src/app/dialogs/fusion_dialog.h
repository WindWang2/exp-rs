// fusion_dialog.h — Phase 11.1 Image Fusion / Pan-sharpening Dialog
#pragma once

#include "raster_processing_dialog_base.h"
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QWidget;

class FusionDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit FusionDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("fusion"); }
    QString dialogTitle() const override { return tr("Image Fusion"); }
    void onRun() override;

private slots:
    void onMethodChanged(int index);
    void onBrowsePan();
    void onBrowseMs();

private:
    void setupUi();

    QComboBox *mPanCombo = nullptr;
    QComboBox *mMsCombo = nullptr;
    QComboBox *mMethodCombo = nullptr;
    QComboBox *mRedCombo = nullptr;
    QComboBox *mGreenCombo = nullptr;
    QComboBox *mBlueCombo = nullptr;
    QLabel *mRedLabel = nullptr;
    QLabel *mGreenLabel = nullptr;
    QLabel *mBlueLabel = nullptr;
    QDoubleSpinBox *mWeightSpin = nullptr;
    QLabel *mWeightLabel = nullptr;
    QVector<QDoubleSpinBox*> mBandWeightSpins;
    QWidget *mBandWeightsWidget = nullptr;
    QFormLayout *mBandWeightsLayout = nullptr;
    QLabel *mStatusLabel = nullptr;
};
