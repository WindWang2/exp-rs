// src/app/dialogs/spatial_filter_dialog.cpp
#include "spatial_filter_dialog.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <cpl_error.h>
#include <cpl_string.h>

SpatialFilterDialog::SpatialFilterDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Spatial Filter"));
    setupUi();
}

void SpatialFilterDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
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

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:"), this));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &SpatialFilterDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &SpatialFilterDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void SpatialFilterDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void SpatialFilterDialog::onRun()
{
    // Validate inputs
    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Spatial Filter"), tr("Please specify an output file."));
        return;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, tr("Spatial Filter"), tr("No valid raster layer selected."));
        return;
    }

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->dataProvider()->dataSourceUri())) {
        QgsMessageLog::logMessage(tr("Failed to open source file: %1").arg(srcDataset.lastError()),
                                  "spatial_filter", Qgis::MessageLevel::Critical);
        return;
    }

    int width = srcDataset.width();
    int height = srcDataset.height();
    int bandCount = srcDataset.bandCount();
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Determine kernel size
    int kernelSize = (m_kernelSizeCombo->currentIndex() == 1) ? 5 : 3;
    int filterIndex = m_filterTypeCombo->currentIndex();

    // Read and filter each band
    std::vector<std::vector<float>> outputBands(bandCount, std::vector<float>(pixelCount));

    for (int b = 0; b < bandCount; ++b) {
        std::vector<float> bandData(pixelCount);
        if (!srcDataset.readBandData(b + 1, bandData.data(), width, height)) {
            QgsMessageLog::logMessage(tr("Failed to read band %1.").arg(b + 1),
                                      "spatial_filter", Qgis::MessageLevel::Critical);
            return;
        }

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

    // Create output file using GDAL
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        QgsMessageLog::logMessage(tr("Failed to get GeoTIFF driver."),
                                  "spatial_filter", Qgis::MessageLevel::Critical);
        return;
    }

    char **options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "LZW");
    GDALDatasetH dstDataset = GDALCreate(driver, outputPath.toUtf8().constData(),
                                          width, height, bandCount, GDT_Float32, options);
    CSLDestroy(options);

    if (!dstDataset) {
        QgsMessageLog::logMessage(tr("Failed to create output file."),
                                  "spatial_filter", Qgis::MessageLevel::Critical);
        return;
    }

    // Copy geotransform and projection from source
    std::array<double, 6> gt = srcDataset.geoTransform();
    GDALSetGeoTransform(dstDataset, gt.data());
    GDALSetProjection(dstDataset, srcDataset.projection().toUtf8().constData());

    // Write all output bands
    for (int b = 0; b < bandCount; ++b) {
        GDALRasterBandH dstBand = GDALGetRasterBand(dstDataset, b + 1);
        if (!dstBand) {
            QgsMessageLog::logMessage(tr("Failed to get output band %1.").arg(b + 1),
                                      "spatial_filter", Qgis::MessageLevel::Critical);
            GDALClose(dstDataset);
            return;
        }
        CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                                   outputBands[b].data(), width, height, GDT_Float32, 0, 0);
        if (err != CE_None) {
            QgsMessageLog::logMessage(tr("Failed to write output band %1.").arg(b + 1),
                                      "spatial_filter", Qgis::MessageLevel::Critical);
            GDALClose(dstDataset);
            return;
        }
    }

    GDALClose(dstDataset);

    QgsMessageLog::logMessage(tr("Spatial filter completed successfully! Output: %1").arg(outputPath),
                              "spatial_filter", Qgis::MessageLevel::Success);
    accept();
}
