// src/app/dialogs/image_enhancement_panel.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

/**
 * Unified Image Enhancement Panel combining multiple processing operations:
 * - Contrast stretch (linear, percent clip, stddev, histogram eq)
 * - Spatial filter (mean, gaussian, median, sobel, laplacian, custom convolution)
 * - Band ratio / IHS
 * - Speckle filter (Lee, Frost, Kuan, Gamma-MAP)
 */
class ImageEnhancementPanel : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit ImageEnhancementPanel(QWidget *parent = nullptr);
    void setRasterLayer(QgsRasterLayer *layer) override;

protected:
    QString toolName() const override { return QStringLiteral("image_enhancement"); }
    QString dialogTitle() const override { return tr("Image Enhancement"); }
    void onRun() override;

private slots:
    void onMethodChanged(int index);
    void onCompleted(const QString &outputPath);
    void onFailed(const QString &errorMessage);

private:
    void setupUi();
    void populateBandCombos();
    void setupStretchOptions(QVBoxLayout *layout);
    void setupFilterOptions(QVBoxLayout *layout);
    void setupBandRatioOptions(QVBoxLayout *layout);
    void setupSpeckleOptions(QVBoxLayout *layout);

    QComboBox *m_methodCombo = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;

    // Stretch options
    QComboBox *m_stretchTypeCombo = nullptr;
    QDoubleSpinBox *m_clipPercentSpin = nullptr;
    QDoubleSpinBox *m_stddevMultSpin = nullptr;
    QLabel *m_clipLabel = nullptr;
    QLabel *m_stddevLabel = nullptr;

    // Filter options
    QComboBox *m_filterTypeCombo = nullptr;
    QComboBox *m_kernelSizeCombo = nullptr;
    QDoubleSpinBox *m_sigmaSpin = nullptr;
    QLineEdit *m_customKernelEdit = nullptr;
    QLabel *m_sigmaLabel = nullptr;
    QLabel *m_customKernelLabel = nullptr;

    // Band ratio options
    QComboBox *m_ratioTypeCombo = nullptr;
    QComboBox *m_band1Combo = nullptr;
    QComboBox *m_band2Combo = nullptr;
    QLabel *m_band1Label = nullptr;
    QLabel *m_band2Label = nullptr;

    // Speckle options
    QComboBox *m_speckleTypeCombo = nullptr;
    QComboBox *m_speckleKernelCombo = nullptr;
    QDoubleSpinBox *m_noiseVarSpin = nullptr;
    QDoubleSpinBox *m_dampingSpin = nullptr;
    QLabel *m_noiseVarLabel = nullptr;
    QLabel *m_dampingLabel = nullptr;

    // Status
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_runButton = nullptr;


};
