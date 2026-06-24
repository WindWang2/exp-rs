// src/app/dialogs/spatial_filter_dialog.cpp
#include "spatial_filter_dialog.h"
#include "async_gdal_runner.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>

SpatialFilterDialog::SpatialFilterDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void SpatialFilterDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Filter type selection
    auto *typeLayout = new QHBoxLayout();
    typeLayout->addWidget(new QLabel(tr("Filter:"), this));
    m_filterTypeCombo = new QComboBox(this);
    m_filterTypeCombo->addItems({tr("Mean"), tr("Gaussian"), tr("Median"),
                                 tr("Sobel"), tr("Laplacian")});
    typeLayout->addWidget(m_filterTypeCombo);
    mainLayout->addLayout(typeLayout);

    // Kernel size selection
    auto *kernelLayout = new QHBoxLayout();
    kernelLayout->addWidget(new QLabel(tr("Kernel Size:"), this));
    m_kernelSizeCombo = new QComboBox(this);
    m_kernelSizeCombo->addItems({tr("3x3"), tr("5x5")});
    kernelLayout->addWidget(m_kernelSizeCombo);
    mainLayout->addLayout(kernelLayout);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);
}

void SpatialFilterDialog::onRun()
{
    // Capture parameters for async execution
    QString sourcePath = m_rasterLayer->source();
    int kernelSize = (m_kernelSizeCombo->currentIndex() == 1) ? 5 : 3;
    int filterIndex = m_filterTypeCombo->currentIndex();

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &SpatialFilterDialog::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &SpatialFilterDialog::onFailed);
    }

    m_runButton->setEnabled(false);

    m_runner->run([this, sourcePath, outputPath = outputPath(), kernelSize, filterIndex]() -> QString {
    try {
        // Open source dataset
        GdalDatasetWrapper srcDataset;
        if (!srcDataset.open(sourcePath)) return QString();

        int width = srcDataset.width();
        int height = srcDataset.height();
        int bandCount = srcDataset.bandCount();
        size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

        // Read and filter each band
        std::vector<std::vector<float>> outputBands(bandCount, std::vector<float>(pixelCount));

        for (int b = 0; b < bandCount; ++b) {
            std::vector<float> bandData(pixelCount);
            if (!srcDataset.readBandData(b + 1, bandData.data(), width, height)) return QString();

            switch (filterIndex) {
            case 0: // Mean
                ImageEnhancement::meanFilter(bandData.data(), outputBands[b].data(),
                                             width, height, kernelSize);
                break;
            case 1: // Gaussian
                ImageEnhancement::gaussianFilter(bandData.data(), outputBands[b].data(),
                                                 width, height, kernelSize);
                break;
            case 2: // Median
                ImageEnhancement::medianFilter(bandData.data(), outputBands[b].data(),
                                               width, height, kernelSize);
                break;
            case 3: // Sobel
                ImageEnhancement::sobelFilter(bandData.data(), outputBands[b].data(),
                                              width, height);
                break;
            case 4: // Laplacian
                ImageEnhancement::laplacianFilter(bandData.data(), outputBands[b].data(),
                                                  width, height);
                break;
            }
        }

        // Create output
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


