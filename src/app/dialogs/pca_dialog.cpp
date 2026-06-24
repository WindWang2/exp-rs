// src/app/dialogs/pca_dialog.cpp
#include "pca_dialog.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QMessageBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>

PcaDialog::PcaDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void PcaDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Number of components
    auto *compLayout = new QHBoxLayout();
    compLayout->addWidget(new QLabel(tr("Components:"), this));
    m_componentsSpin = new QSpinBox(this);
    m_componentsSpin->setRange(1, 10);
    m_componentsSpin->setValue(3);
    compLayout->addWidget(m_componentsSpin);
    mainLayout->addLayout(compLayout);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);
}

void PcaDialog::onRun()
{
    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->source())) {
        QgsMessageLog::logMessage(tr("Failed to open source file: %1").arg(srcDataset.lastError()),
                                  "pca", Qgis::MessageLevel::Critical);
        return;
    }

    int width = srcDataset.width();
    int height = srcDataset.height();
    int bandCount = srcDataset.bandCount();
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    int numComponents = m_componentsSpin->value();

    if (numComponents > bandCount) {
        QMessageBox::warning(this, tr("PCA"),
                             tr("Number of components (%1) exceeds band count (%2).")
                                 .arg(numComponents).arg(bandCount));
        return;
    }

    // Read all bands
    std::vector<std::vector<float>> allBands(bandCount, std::vector<float>(pixelCount));
    for (int b = 0; b < bandCount; ++b) {
        if (!srcDataset.readBandData(b + 1, allBands[b].data(), width, height)) {
            QgsMessageLog::logMessage(tr("Failed to read band %1.").arg(b + 1),
                                      "pca", Qgis::MessageLevel::Critical);
            return;
        }
    }

    // Run PCA
    ImageEnhancement::PcaResult pcaResult = ImageEnhancement::pca(allBands, numComponents);

    // Create output
    QString error;
    GdalDatasetGuard dstGuard(createOutputTiff(outputPath(), width, height, numComponents,
                                               GDT_Float32, srcDataset.geoTransform(),
                                               srcDataset.projection(), &error));
    if (!dstGuard) {
        QgsMessageLog::logMessage(tr("Failed to create output file: %1").arg(error),
                                  "pca", Qgis::MessageLevel::Critical);
        return;
    }

    // Write each component as a band
    for (int c = 0; c < numComponents; ++c) {
        GDALRasterBandH dstBand = GDALGetRasterBand(dstGuard.get(), c + 1);
        if (!dstBand) {
            QgsMessageLog::logMessage(tr("Failed to get output band %1.").arg(c + 1),
                                      "pca", Qgis::MessageLevel::Critical);
            return;
        }
        CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                                   pcaResult.output[c].data(), width, height, GDT_Float32, 0, 0);
        if (err != CE_None) {
            QgsMessageLog::logMessage(tr("Failed to write output band %1.").arg(c + 1),
                                      "pca", Qgis::MessageLevel::Critical);
            return;
        }
    }

    // Log variance explained
    QString varianceMsg;
    for (int c = 0; c < numComponents; ++c) {
        varianceMsg += tr("PC%1: %2%  ").arg(c + 1)
                           .arg(pcaResult.explainedVariance[c] * 100.0f, 0, 'f', 1);
    }

    handleCompleted(outputPath());
}

void PcaDialog::onCompleted(const QString &outputPath)
{
    handleCompleted(outputPath);
}

void PcaDialog::onFailed(const QString &errorMessage)
{
    handleFailed(errorMessage);
}
