// src/app/dialogs/pca_dialog.cpp
#include "pca_dialog.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QFileDialog>
#include <QMessageBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>
#include <cpl_string.h>

PcaDialog::PcaDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("PCA"));
    setupUi();
}

void PcaDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
    if (layer && layer->isValid()) {
        m_componentsSpin->setMaximum(layer->bandCount());
    }
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

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:"), this));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &PcaDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &PcaDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void PcaDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void PcaDialog::onRun()
{
    // Validate inputs
    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("PCA"), tr("Please specify an output file."));
        return;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, tr("PCA"), tr("No valid raster layer selected."));
        return;
    }

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->dataProvider()->dataSourceUri())) {
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
        GdalDatasetGuard dstGuard(createOutputTiff(outputPath, width, height, bandCount,
                                                   GDT_Float32, srcDataset.geoTransform(),
                                                   srcDataset.projection(), &error));
        if (!dstGuard) return QString();

    // Write each component as a band
    for (int c = 0; c < numComponents; ++c) {
        GDALRasterBandH dstBand = GDALGetRasterBand(dstDataset, c + 1);
        if (!dstBand) {
            QgsMessageLog::logMessage(tr("Failed to get output band %1.").arg(c + 1),
                                      "pca", Qgis::MessageLevel::Critical);
            GDALClose(dstDataset);
            return;
        }
        CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                                   pcaResult.output[c].data(), width, height, GDT_Float32, 0, 0);
        if (err != CE_None) {
            QgsMessageLog::logMessage(tr("Failed to write output band %1.").arg(c + 1),
                                      "pca", Qgis::MessageLevel::Critical);
            GDALClose(dstDataset);
            return;
        }
    }

    GDALClose(dstDataset);

    // Log variance explained
    QString varianceMsg;
    for (int c = 0; c < numComponents; ++c) {
        varianceMsg += tr("PC%1: %2%  ").arg(c + 1)
                           .arg(pcaResult.explainedVariance[c] * 100.0f, 0, 'f', 1);
    }

    QgsMessageLog::logMessage(tr("PCA completed successfully! Variance explained: %1. Output: %2")
                                  .arg(varianceMsg, outputPath),
                              "pca", Qgis::MessageLevel::Success);
    accept();
}
