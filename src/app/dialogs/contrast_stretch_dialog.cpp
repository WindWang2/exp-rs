// src/app/dialogs/contrast_stretch_dialog.cpp
#include "contrast_stretch_dialog.h"
#include "dialog_help_catalog.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>

#include <gdal.h>
#include <cpl_error.h>

ContrastStretchDialog::ContrastStretchDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void ContrastStretchDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    setupHelpBanner(mainLayout);
// Method selection
    auto *methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:"), this));
    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItems({tr("Linear"), tr("Percentage Clip"),
                             tr("Std Dev"), tr("Histogram Equalization")});
    SicnuDialogHelp::tip( m_methodCombo, tr(
      "拉伸方法：\n"
      "• Linear：最小–最大线性\n"
      "• Percentage Clip：两端裁剪后再拉伸（抑制极端值）\n"
      "• Std Dev：均值±K×标准差\n"
      "• Histogram Equalization：直方图均衡" ) );
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContrastStretchDialog::onMethodChanged);
    methodLayout->addWidget(m_methodCombo);
    mainLayout->addLayout(methodLayout);

    // Clip percentage parameter
    auto *clipLayout = new QHBoxLayout();
    m_clipLabel = new QLabel(tr("Clip %:"), this);
    clipLayout->addWidget(m_clipLabel);
    m_clipSpin = new QDoubleSpinBox(this);
    m_clipSpin->setRange(0.1, 50.0);
    m_clipSpin->setValue(2.0);
    m_clipSpin->setSingleStep(0.5);
    m_clipSpin->setDecimals(1);
    m_clipSpin->setSuffix("%");
    SicnuDialogHelp::tip( m_clipSpin, tr( "百分比裁剪：两端各舍弃该比例的像元后再拉伸。常用 1–2%。" ) );
    clipLayout->addWidget(m_clipSpin);
    mainLayout->addLayout(clipLayout);

    // Std Dev K parameter
    auto *stddevLayout = new QHBoxLayout();
    m_stddevLabel = new QLabel(tr("Std Dev K:"), this);
    stddevLayout->addWidget(m_stddevLabel);
    m_stddevSpin = new QDoubleSpinBox(this);
    m_stddevSpin->setRange(0.1, 10.0);
    m_stddevSpin->setValue(2.0);
    m_stddevSpin->setSingleStep(0.5);
    m_stddevSpin->setDecimals(1);
    SicnuDialogHelp::tip( m_stddevSpin, tr( "标准差倍数 K：拉伸到 mean±K·σ。常用 2。" ) );
    stddevLayout->addWidget(m_stddevSpin);
    mainLayout->addLayout(stddevLayout);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);

    // Initialize visibility
    onMethodChanged(0);
}

void ContrastStretchDialog::onMethodChanged(int index)
{
    // Linear: no extra params
    // Percentage Clip: show clip %
    // Std Dev: show K
    // Histogram Equalization: no extra params
    m_clipLabel->setVisible(index == 1);
    m_clipSpin->setVisible(index == 1);
    m_stddevLabel->setVisible(index == 2);
    m_stddevSpin->setVisible(index == 2);
}

void ContrastStretchDialog::onRun()
{
    // Capture parameters for async execution
    QString sourcePath = m_rasterLayer->source();
    int methodIndex = m_methodCombo->currentIndex();
    double clipValue = m_clipSpin->value();
    double stddevValue = m_stddevSpin->value();

    runGdalTask([sourcePath, outputPath = outputPath(), methodIndex, clipValue, stddevValue]() -> QString {
    try {
        // Open source dataset
        GdalDatasetWrapper srcDataset;
        if (!srcDataset.open(sourcePath)) return QString();

        int width = srcDataset.width();
        int height = srcDataset.height();
        int bandCount = srcDataset.bandCount();
        size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

        // Read all bands
        std::vector<std::vector<float>> allBands(bandCount, std::vector<float>(pixelCount));
        for (int b = 0; b < bandCount; ++b) {
            if (!srcDataset.readBandData(b + 1, allBands[b].data(), width, height)) return QString();
        }

        // Apply stretch to each band
        std::vector<std::vector<float>> outputBands(bandCount, std::vector<float>(pixelCount));

        for (int b = 0; b < bandCount; ++b) {
            switch (methodIndex) {
            case 0: // Linear
            {
                float minVal = *std::min_element(allBands[b].begin(), allBands[b].end());
                float maxVal = *std::max_element(allBands[b].begin(), allBands[b].end());
                ImageEnhancement::linearStretch(allBands[b].data(), outputBands[b].data(),
                                                pixelCount, minVal, maxVal);
                break;
            }
            case 1: // Percentage Clip
                ImageEnhancement::percentClipStretch(allBands[b].data(), outputBands[b].data(),
                                                     pixelCount,
                                                     static_cast<float>(clipValue));
                break;
            case 2: // Std Dev
                ImageEnhancement::stddevStretch(allBands[b].data(), outputBands[b].data(),
                                                pixelCount,
                                                static_cast<float>(stddevValue));
                break;
            case 3: // Histogram Equalization
                ImageEnhancement::histogramEqualize(allBands[b].data(), outputBands[b].data(),
                                                    pixelCount);
                break;
            }
        }

        QString error;
        if (!writeGdalOutput(outputPath, width, height, outputBands,
                             srcDataset.geoTransform(), srcDataset.projection(), &error))
            return QString();

        return outputPath;
    } catch (const std::runtime_error &) {
        return QString();
    }
    });
}


