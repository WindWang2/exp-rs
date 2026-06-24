// src/app/dialogs/speckle_filter_dialog.cpp
#include "speckle_filter_dialog.h"
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

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>

SpeckleFilterDialog::SpeckleFilterDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void SpeckleFilterDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Filter type selection
    auto *typeLayout = new QHBoxLayout();
    typeLayout->addWidget(new QLabel(tr("Filter:"), this));
    m_filterTypeCombo = new QComboBox(this);
    m_filterTypeCombo->addItems({tr("Lee"), tr("Frost"), tr("Kuan"), tr("Gamma-MAP")});
    typeLayout->addWidget(m_filterTypeCombo);
    mainLayout->addLayout(typeLayout);

    // Kernel size selection
    auto *kernelLayout = new QHBoxLayout();
    kernelLayout->addWidget(new QLabel(tr("Window Size:"), this));
    m_kernelSizeCombo = new QComboBox(this);
    m_kernelSizeCombo->addItems({tr("3x3"), tr("5x5"), tr("7x7")});
    m_kernelSizeCombo->setCurrentIndex(1); // default 5x5
    kernelLayout->addWidget(m_kernelSizeCombo);
    mainLayout->addLayout(kernelLayout);

    // Noise variance (for Lee, Kuan, Gamma-MAP)
    auto *noiseLayout = new QHBoxLayout();
    noiseLayout->addWidget(new QLabel(tr("Noise Variance:"), this));
    m_noiseVarSpin = new QDoubleSpinBox(this);
    m_noiseVarSpin->setRange(0.001, 10.0);
    m_noiseVarSpin->setValue(1.0);
    m_noiseVarSpin->setSingleStep(0.1);
    m_noiseVarSpin->setDecimals(3);
    noiseLayout->addWidget(m_noiseVarSpin);
    mainLayout->addLayout(noiseLayout);

    // Damping factor (Frost only)
    auto *dampingLayout = new QHBoxLayout();
    m_dampingLabel = new QLabel(tr("Damping Factor:"), this);
    dampingLayout->addWidget(m_dampingLabel);
    m_dampingSpin = new QDoubleSpinBox(this);
    m_dampingSpin->setRange(0.1, 10.0);
    m_dampingSpin->setValue(2.0);
    m_dampingSpin->setSingleStep(0.5);
    m_dampingSpin->setDecimals(1);
    dampingLayout->addWidget(m_dampingSpin);
    mainLayout->addLayout(dampingLayout);

    // Initially show/hide based on filter type
    connect(m_filterTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SpeckleFilterDialog::onFilterTypeChanged);
    onFilterTypeChanged(0);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);
}

void SpeckleFilterDialog::onFilterTypeChanged(int index)
{
    // Frost uses damping factor; others use noise variance
    bool isFrost = (index == 1);
    m_dampingLabel->setVisible(isFrost);
    m_dampingSpin->setVisible(isFrost);
    m_noiseVarSpin->setVisible(!isFrost);

    // Update noise variance label
    auto *noiseLabel = qobject_cast<QLabel *>(m_noiseVarSpin->parentWidget()->layout()->itemAt(0)->widget());
    if (noiseLabel) {
        noiseLabel->setVisible(!isFrost);
    }
}

void SpeckleFilterDialog::onRun()
{
    // Capture parameters for async execution
    QString sourcePath = m_rasterLayer->source();
    int kernelSize = 3;
    switch (m_kernelSizeCombo->currentIndex()) {
    case 0: kernelSize = 3; break;
    case 1: kernelSize = 5; break;
    case 2: kernelSize = 7; break;
    }
    int filterIndex = m_filterTypeCombo->currentIndex();
    float noiseVar = static_cast<float>(m_noiseVarSpin->value());
    float damping = static_cast<float>(m_dampingSpin->value());

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &SpeckleFilterDialog::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &SpeckleFilterDialog::onFailed);
    }

    m_runButton->setEnabled(false);

    m_runner->run([this, sourcePath, outputPath = outputPath(), kernelSize, filterIndex,
                   noiseVar, damping]() -> QString {
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
            case 0: // Lee
                ImageEnhancement::leeFilter(bandData.data(), outputBands[b].data(),
                                            width, height, kernelSize, noiseVar);
                break;
            case 1: // Frost
                ImageEnhancement::frostFilter(bandData.data(), outputBands[b].data(),
                                              width, height, kernelSize, damping);
                break;
            case 2: // Kuan
                ImageEnhancement::kuanFilter(bandData.data(), outputBands[b].data(),
                                             width, height, kernelSize, noiseVar);
                break;
            case 3: // Gamma-MAP
                ImageEnhancement::gammaMapFilter(bandData.data(), outputBands[b].data(),
                                                 width, height, kernelSize, noiseVar);
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

void SpeckleFilterDialog::onCompleted(const QString &outputPath)
{
    handleCompleted(outputPath);
}

void SpeckleFilterDialog::onFailed(const QString &error)
{
    handleFailed(error);
}
