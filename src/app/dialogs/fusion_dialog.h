// fusion_dialog.h — Phase 11.1 Image Fusion / Pan-sharpening Dialog
#pragma once

#include <QDialog>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QLabel;
class QWidget;
class QgsRasterLayer;

class FusionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FusionDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);
    QString outputPath() const;

private slots:
    void onRun();
    void onMethodChanged(int index);
    void onCompleted(const QString &outputPath);
    void onFailed(const QString &errorMessage);
    void onBrowsePan();
    void onBrowseMs();
    void onBrowseOutput();

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
    QLineEdit *mOutputEdit = nullptr;
    QLabel *mStatusLabel = nullptr;
};
