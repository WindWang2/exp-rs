// src/app/dialogs/spectral_index_dialog.cpp
#include "spectral_index_dialog.h"
#include "processing/algorithms/spectral_indices.h"
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

SpectralIndexDialog::SpectralIndexDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Spectral Index"));
    setupUi();
}

void SpectralIndexDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
    populateBandCombos();
}

void SpectralIndexDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Index selection
    auto *idxLayout = new QHBoxLayout();
    idxLayout->addWidget(new QLabel(tr("Index:")));
    m_indexCombo = new QComboBox(this);
    m_indexCombo->addItems({tr("NDVI"), tr("EVI"), tr("SAVI"), tr("NDWI"), tr("NDBI"), tr("MNDWI")});
    connect(m_indexCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SpectralIndexDialog::onIndexChanged);
    idxLayout->addWidget(m_indexCombo);
    mainLayout->addLayout(idxLayout);

    // Band selectors - Row 1
    auto *bandLayout = new QHBoxLayout();
    m_nirLabel = new QLabel(tr("NIR:"), this);
    bandLayout->addWidget(m_nirLabel);
    m_nirCombo = new QComboBox(this);
    bandLayout->addWidget(m_nirCombo);
    m_redLabel = new QLabel(tr("Red:"), this);
    bandLayout->addWidget(m_redLabel);
    m_redCombo = new QComboBox(this);
    bandLayout->addWidget(m_redCombo);
    mainLayout->addLayout(bandLayout);

    // Band selectors - Row 2
    auto *bandLayout2 = new QHBoxLayout();
    m_greenLabel = new QLabel(tr("Green:"), this);
    bandLayout2->addWidget(m_greenLabel);
    m_greenCombo = new QComboBox(this);
    bandLayout2->addWidget(m_greenCombo);
    m_blueLabel = new QLabel(tr("Blue:"), this);
    bandLayout2->addWidget(m_blueLabel);
    m_blueCombo = new QComboBox(this);
    bandLayout2->addWidget(m_blueCombo);
    m_swirLabel = new QLabel(tr("SWIR:"), this);
    bandLayout2->addWidget(m_swirLabel);
    m_swirCombo = new QComboBox(this);
    bandLayout2->addWidget(m_swirCombo);
    mainLayout->addLayout(bandLayout2);

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:")));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &SpectralIndexDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &SpectralIndexDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    // Initialize band visibility
    updateBandVisibility();
}

void SpectralIndexDialog::populateBandCombos()
{
    if (!m_rasterLayer || !m_rasterLayer->isValid())
        return;

    int bandCount = m_rasterLayer->bandCount();

    // Clear existing items
    m_nirCombo->clear();
    m_redCombo->clear();
    m_greenCombo->clear();
    m_blueCombo->clear();
    m_swirCombo->clear();

    // Populate with band numbers
    for (int i = 1; i <= bandCount; ++i) {
        QString bandName = tr("Band %1").arg(i);
        m_nirCombo->addItem(bandName, i);
        m_redCombo->addItem(bandName, i);
        m_greenCombo->addItem(bandName, i);
        m_blueCombo->addItem(bandName, i);
        m_swirCombo->addItem(bandName, i);
    }

    // Set default band mappings (typical for Landsat/Sentinel-2)
    if (bandCount >= 4) {
        m_nirCombo->setCurrentIndex(3);  // Band 4 = NIR
        m_redCombo->setCurrentIndex(2);  // Band 3 = Red
        m_greenCombo->setCurrentIndex(1); // Band 2 = Green
        m_blueCombo->setCurrentIndex(0); // Band 1 = Blue
    }
    if (bandCount >= 5) {
        m_swirCombo->setCurrentIndex(4); // Band 5 = SWIR
    }
}

void SpectralIndexDialog::updateBandVisibility()
{
    int index = m_indexCombo->currentIndex();

    // Show/hide band selectors based on selected index
    // NDVI: NIR, Red
    // EVI: NIR, Red, Blue
    // SAVI: NIR, Red
    // NDWI: Green, NIR
    // NDBI: SWIR, NIR
    // MNDWI: Green, SWIR

    m_nirCombo->setVisible(true);
    m_nirLabel->setVisible(true);

    m_redCombo->setVisible(index == 0 || index == 1 || index == 2); // NDVI, EVI, SAVI
    m_redLabel->setVisible(index == 0 || index == 1 || index == 2);

    m_greenCombo->setVisible(index == 3 || index == 5); // NDWI, MNDWI
    m_greenLabel->setVisible(index == 3 || index == 5);

    m_blueCombo->setVisible(index == 1); // EVI only
    m_blueLabel->setVisible(index == 1);

    m_swirCombo->setVisible(index == 4 || index == 5); // NDBI, MNDWI
    m_swirLabel->setVisible(index == 4 || index == 5);
}

void SpectralIndexDialog::onIndexChanged(int index)
{
    updateBandVisibility();
}

void SpectralIndexDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void SpectralIndexDialog::onRun()
{
    // Validate inputs
    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Spectral Index"), tr("Please specify an output file."));
        return;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, tr("Spectral Index"), tr("No valid raster layer selected."));
        return;
    }

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->dataProvider()->dataSourceUri())) {
        QgsMessageLog::logMessage(tr("Failed to open source file: %1").arg(srcDataset.lastError()),
                                  "spectral_index", Qgis::MessageLevel::Critical);
        return;
    }

    int width = srcDataset.width();
    int height = srcDataset.height();
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Read required bands
    int index = m_indexCombo->currentIndex();

    // Helper lambda to read a band
    auto readBand = [&](int bandNum) -> std::vector<float> {
        std::vector<float> buffer(pixelCount);
        if (!srcDataset.readBandData(bandNum, buffer.data(), width, height)) {
            return {};
        }
        return buffer;
    };

    // Read bands based on selected index
    std::vector<float> nir, red, green, blue, swir, output(pixelCount);
    bool success = false;

    switch (index) {
    case 0: // NDVI
    {
        nir = readBand(m_nirCombo->currentData().toInt());
        red = readBand(m_redCombo->currentData().toInt());
        if (nir.empty() || red.empty()) {
            QgsMessageLog::logMessage(tr("Failed to read band data."),
                                      "spectral_index", Qgis::MessageLevel::Critical);
            return;
        }
        success = SpectralIndices::ndvi(nir.data(), red.data(), output.data(), pixelCount);
        break;
    }
    case 1: // EVI
    {
        nir = readBand(m_nirCombo->currentData().toInt());
        red = readBand(m_redCombo->currentData().toInt());
        blue = readBand(m_blueCombo->currentData().toInt());
        if (nir.empty() || red.empty() || blue.empty()) {
            QgsMessageLog::logMessage(tr("Failed to read band data."),
                                      "spectral_index", Qgis::MessageLevel::Critical);
            return;
        }
        success = SpectralIndices::evi(nir.data(), red.data(), blue.data(), output.data(), pixelCount);
        break;
    }
    case 2: // SAVI
    {
        nir = readBand(m_nirCombo->currentData().toInt());
        red = readBand(m_redCombo->currentData().toInt());
        if (nir.empty() || red.empty()) {
            QgsMessageLog::logMessage(tr("Failed to read band data."),
                                      "spectral_index", Qgis::MessageLevel::Critical);
            return;
        }
        success = SpectralIndices::savi(nir.data(), red.data(), output.data(), pixelCount);
        break;
    }
    case 3: // NDWI
    {
        green = readBand(m_greenCombo->currentData().toInt());
        nir = readBand(m_nirCombo->currentData().toInt());
        if (green.empty() || nir.empty()) {
            QgsMessageLog::logMessage(tr("Failed to read band data."),
                                      "spectral_index", Qgis::MessageLevel::Critical);
            return;
        }
        success = SpectralIndices::ndwi(green.data(), nir.data(), output.data(), pixelCount);
        break;
    }
    case 4: // NDBI
    {
        swir = readBand(m_swirCombo->currentData().toInt());
        nir = readBand(m_nirCombo->currentData().toInt());
        if (swir.empty() || nir.empty()) {
            QgsMessageLog::logMessage(tr("Failed to read band data."),
                                      "spectral_index", Qgis::MessageLevel::Critical);
            return;
        }
        success = SpectralIndices::ndbi(swir.data(), nir.data(), output.data(), pixelCount);
        break;
    }
    case 5: // MNDWI
    {
        green = readBand(m_greenCombo->currentData().toInt());
        swir = readBand(m_swirCombo->currentData().toInt());
        if (green.empty() || swir.empty()) {
            QgsMessageLog::logMessage(tr("Failed to read band data."),
                                      "spectral_index", Qgis::MessageLevel::Critical);
            return;
        }
        success = SpectralIndices::mndwi(green.data(), swir.data(), output.data(), pixelCount);
        break;
    }
    }

    if (!success) {
        QgsMessageLog::logMessage(tr("Failed to calculate spectral index."),
                                  "spectral_index", Qgis::MessageLevel::Critical);
        return;
    }

    // Create output file using GDAL
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        QgsMessageLog::logMessage(tr("Failed to get GeoTIFF driver."),
                                  "spectral_index", Qgis::MessageLevel::Critical);
        return;
    }

    char **options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "LZW");
    GDALDatasetH dstDataset = GDALCreate(driver, outputPath.toUtf8().constData(),
                                          width, height, 1, GDT_Float32, options);
    CSLDestroy(options);

    if (!dstDataset) {
        QgsMessageLog::logMessage(tr("Failed to create output file."),
                                  "spectral_index", Qgis::MessageLevel::Critical);
        return;
    }

    // Copy geotransform and projection from source
    std::array<double, 6> gt = srcDataset.geoTransform();
    GDALSetGeoTransform(dstDataset, gt.data());
    GDALSetProjection(dstDataset, srcDataset.projection().toUtf8().constData());

    // Write output band
    GDALRasterBandH dstBand = GDALGetRasterBand(dstDataset, 1);
    if (!dstBand) {
        QgsMessageLog::logMessage(tr("Failed to get output band."),
                                  "spectral_index", Qgis::MessageLevel::Critical);
        GDALClose(dstDataset);
        return;
    }
    CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                               output.data(), width, height, GDT_Float32, 0, 0);

    GDALClose(dstDataset);

    if (err != CE_None) {
        QgsMessageLog::logMessage(tr("Failed to write output file."),
                                  "spectral_index", Qgis::MessageLevel::Critical);
        return;
    }

    QgsMessageLog::logMessage(tr("Spectral index calculation completed successfully! Output: %1").arg(outputPath),
                              "spectral_index", Qgis::MessageLevel::Success);
    accept();
}
