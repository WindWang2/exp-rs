// src/app/dialogs/band_ratio_dialog.cpp
#include "band_ratio_dialog.h"
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

BandRatioDialog::BandRatioDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Band Ratio / IHS"));
    setupUi();
}

void BandRatioDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
    populateBandCombos();
}

void BandRatioDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Mode selection
    auto *modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(tr("Mode:"), this));
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems({tr("Band Ratio"), tr("IHS Transform")});
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BandRatioDialog::onModeChanged);
    modeLayout->addWidget(m_modeCombo);
    mainLayout->addLayout(modeLayout);

    // Band Ratio selectors
    auto *ratioLayout = new QHBoxLayout();
    m_band1Label = new QLabel(tr("Numerator:"), this);
    ratioLayout->addWidget(m_band1Label);
    m_band1Combo = new QComboBox(this);
    ratioLayout->addWidget(m_band1Combo);
    m_band2Label = new QLabel(tr("Denominator:"), this);
    ratioLayout->addWidget(m_band2Label);
    m_band2Combo = new QComboBox(this);
    ratioLayout->addWidget(m_band2Combo);
    mainLayout->addLayout(ratioLayout);

    // IHS selectors
    auto *ihsLayout = new QHBoxLayout();
    m_redLabel = new QLabel(tr("R:"), this);
    ihsLayout->addWidget(m_redLabel);
    m_redCombo = new QComboBox(this);
    ihsLayout->addWidget(m_redCombo);
    m_greenLabel = new QLabel(tr("G:"), this);
    ihsLayout->addWidget(m_greenLabel);
    m_greenCombo = new QComboBox(this);
    ihsLayout->addWidget(m_greenCombo);
    m_blueLabel = new QLabel(tr("B:"), this);
    ihsLayout->addWidget(m_blueLabel);
    m_blueCombo = new QComboBox(this);
    ihsLayout->addWidget(m_blueCombo);
    mainLayout->addLayout(ihsLayout);

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:"), this));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &BandRatioDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &BandRatioDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    // Initialize visibility
    onModeChanged(0);
}

void BandRatioDialog::populateBandCombos()
{
    if (!m_rasterLayer || !m_rasterLayer->isValid())
        return;

    int bandCount = m_rasterLayer->bandCount();

    // Clear existing items
    m_band1Combo->clear();
    m_band2Combo->clear();
    m_redCombo->clear();
    m_greenCombo->clear();
    m_blueCombo->clear();

    // Populate with band numbers
    for (int i = 1; i <= bandCount; ++i) {
        QString bandName = tr("Band %1").arg(i);
        m_band1Combo->addItem(bandName, i);
        m_band2Combo->addItem(bandName, i);
        m_redCombo->addItem(bandName, i);
        m_greenCombo->addItem(bandName, i);
        m_blueCombo->addItem(bandName, i);
    }

    // Set defaults
    if (bandCount >= 2) {
        m_band1Combo->setCurrentIndex(0);
        m_band2Combo->setCurrentIndex(1);
    }
    if (bandCount >= 3) {
        m_redCombo->setCurrentIndex(0);
        m_greenCombo->setCurrentIndex(1);
        m_blueCombo->setCurrentIndex(2);
    }
}

void BandRatioDialog::onModeChanged(int index)
{
    // Band Ratio mode: show ratio controls
    // IHS mode: show RGB controls
    bool isRatio = (index == 0);
    m_band1Label->setVisible(isRatio);
    m_band1Combo->setVisible(isRatio);
    m_band2Label->setVisible(isRatio);
    m_band2Combo->setVisible(isRatio);

    m_redLabel->setVisible(!isRatio);
    m_redCombo->setVisible(!isRatio);
    m_greenLabel->setVisible(!isRatio);
    m_greenCombo->setVisible(!isRatio);
    m_blueLabel->setVisible(!isRatio);
    m_blueCombo->setVisible(!isRatio);
}

void BandRatioDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void BandRatioDialog::onRun()
{
    // Validate inputs
    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Band Ratio / IHS"), tr("Please specify an output file."));
        return;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, tr("Band Ratio / IHS"), tr("No valid raster layer selected."));
        return;
    }

    // Open source dataset
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(m_rasterLayer->dataProvider()->dataSourceUri())) {
        QgsMessageLog::logMessage(tr("Failed to open source file: %1").arg(srcDataset.lastError()),
                                  "band_ratio", Qgis::MessageLevel::Critical);
        return;
    }

    int width = srcDataset.width();
    int height = srcDataset.height();
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    int modeIndex = m_modeCombo->currentIndex();
    int outBandCount = (modeIndex == 0) ? 1 : 3; // ratio=1 band, IHS=3 bands

    // Read required bands
    auto readBand = [&](int bandNum) -> std::vector<float> {
        std::vector<float> buffer(pixelCount);
        if (!srcDataset.readBandData(bandNum, buffer.data(), width, height)) {
            return {};
        }
        return buffer;
    };

    std::vector<std::vector<float>> outputBands(outBandCount, std::vector<float>(pixelCount));

    if (modeIndex == 0) {
        // Band Ratio
        std::vector<float> band1 = readBand(m_band1Combo->currentData().toInt());
        std::vector<float> band2 = readBand(m_band2Combo->currentData().toInt());

        if (band1.empty() || band2.empty()) {
            QgsMessageLog::logMessage(tr("Failed to read band data."),
                                      "band_ratio", Qgis::MessageLevel::Critical);
            return;
        }

        ImageEnhancement::bandRatio(band1.data(), band2.data(),
                                    outputBands[0].data(), pixelCount);
    } else {
        // IHS Transform
        std::vector<float> r = readBand(m_redCombo->currentData().toInt());
        std::vector<float> g = readBand(m_greenCombo->currentData().toInt());
        std::vector<float> b = readBand(m_blueCombo->currentData().toInt());

        if (r.empty() || g.empty() || b.empty()) {
            QgsMessageLog::logMessage(tr("Failed to read band data."),
                                      "band_ratio", Qgis::MessageLevel::Critical);
            return;
        }

        // Transform each pixel from RGB to IHS
        for (size_t i = 0; i < pixelCount; ++i) {
            float ii, h, s;
            ImageEnhancement::rgbToIhs(r[i], g[i], b[i], ii, h, s);
            outputBands[0][i] = ii;
            outputBands[1][i] = h;
            outputBands[2][i] = s;
        }
    }

    // Create output file using GDAL
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        QgsMessageLog::logMessage(tr("Failed to get GeoTIFF driver."),
                                  "band_ratio", Qgis::MessageLevel::Critical);
        return;
    }

    char **options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "LZW");
    GDALDatasetH dstDataset = GDALCreate(driver, outputPath.toUtf8().constData(),
                                          width, height, outBandCount, GDT_Float32, options);
    CSLDestroy(options);

    if (!dstDataset) {
        QgsMessageLog::logMessage(tr("Failed to create output file."),
                                  "band_ratio", Qgis::MessageLevel::Critical);
        return;
    }

    // Copy geotransform and projection from source
    std::array<double, 6> gt = srcDataset.geoTransform();
    GDALSetGeoTransform(dstDataset, gt.data());
    GDALSetProjection(dstDataset, srcDataset.projection().toUtf8().constData());

    // Write all output bands
    for (int b = 0; b < outBandCount; ++b) {
        GDALRasterBandH dstBand = GDALGetRasterBand(dstDataset, b + 1);
        if (!dstBand) {
            QgsMessageLog::logMessage(tr("Failed to get output band %1.").arg(b + 1),
                                      "band_ratio", Qgis::MessageLevel::Critical);
            GDALClose(dstDataset);
            return;
        }
        CPLErr err = GDALRasterIO(dstBand, GF_Write, 0, 0, width, height,
                                   outputBands[b].data(), width, height, GDT_Float32, 0, 0);
        if (err != CE_None) {
            QgsMessageLog::logMessage(tr("Failed to write output band %1.").arg(b + 1),
                                      "band_ratio", Qgis::MessageLevel::Critical);
            GDALClose(dstDataset);
            return;
        }
    }

    GDALClose(dstDataset);

    QgsMessageLog::logMessage(tr("Band ratio / IHS completed successfully! Output: %1").arg(outputPath),
                              "band_ratio", Qgis::MessageLevel::Success);
    accept();
}
