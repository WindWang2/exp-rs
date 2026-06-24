// src/app/dialogs/contrast_stretch_dialog.cpp
#include "contrast_stretch_dialog.h"
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

    // Method selection
    auto *methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:"), this));
    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItems({tr("Linear"), tr("Percentage Clip"),
                             tr("Std Dev"), tr("Histogram Equalization")});
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

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &ContrastStretchDialog::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &ContrastStretchDialog::onFailed);
    }

    m_runButton->setEnabled(false);

    m_runner->run([this, sourcePath, outputPath = outputPath(), methodIndex, clipValue, stddevValue]() -> QString {
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

        // Create output file using GDAL
        QString error;
        GdalDatasetGuard dstGuard(createOutputTiff(outputPath, width, height, bandCount,
                                                   GDT_Float32, srcDataset.geoTransform(),
                                                   srcDataset.projection(), &error));
        if (!dstGuard) return QString();

        // Write all output bands
        for (int b = 0; b < bandCount; ++b) {
            GDALRasterBandH dstBand = GDALGetRasterBand(dstGuard.get(), b + 1);
            if (!dstBand) return QString();
            GDAL_SAFE_CALL( GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                            outputBands[b].data(), width, height, GDT_Float32, 0, 0),
                            "Failed to write output band" );
        }

        return outputPath;
    } catch (const std::runtime_error &) {
        return QString();
    }
    });
}

void ContrastStretchDialog::onCompleted(const QString &outputPath)
{
    handleCompleted(outputPath);
}

void ContrastStretchDialog::onFailed(const QString &error)
{
    handleFailed(error);
}
